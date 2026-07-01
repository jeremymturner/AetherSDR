#include "IcomBackend.h"

#include "IcomRadioCapabilities.h"
#include "LogManager.h"
#include "RadioDiscovery.h"
#include "SecretStore.h"

#include <QDateTime>

#include <cstdlib>

namespace AetherSDR {

namespace {

qint64 nowNs()
{
    return QDateTime::currentMSecsSinceEpoch() * 1000000LL;
}

// --- PROVISIONAL filter-slot boundaries --------------------------------------
//
// Icom exposes three discrete DSP filter slots per mode (FIL1/FIL2/FIL3), not
// arbitrary Hz edges.  The nominal widths below are the radios' factory
// defaults for each slot; every one of them is user-editable in the DSP
// FILTER menu, so these are PROVISIONAL mapping anchors, not invariants.  We
// pick the slot whose nominal width is closest to the requested bandwidth.
//
// Values in Hz.  Grouped by the classes of mode that share a filter shape.

// SSB (LSB/USB) — the classic 3.0 / 2.4 / 1.8 kHz stack (FIL1/2/3).
inline constexpr int kProvisionalSsbFil1Hz = 3000;
inline constexpr int kProvisionalSsbFil2Hz = 2400;
inline constexpr int kProvisionalSsbFil3Hz = 1800;

// CW / CW-R — 1.2 / 0.5 / 0.25 kHz.
inline constexpr int kProvisionalCwFil1Hz = 1200;
inline constexpr int kProvisionalCwFil2Hz = 500;
inline constexpr int kProvisionalCwFil3Hz = 250;

// RTTY / RTTY-R — 2.4 / 0.5 / 0.25 kHz.
inline constexpr int kProvisionalRttyFil1Hz = 2400;
inline constexpr int kProvisionalRttyFil2Hz = 500;
inline constexpr int kProvisionalRttyFil3Hz = 250;

// AM — 9.0 / 6.0 / 3.0 kHz.
inline constexpr int kProvisionalAmFil1Hz = 9000;
inline constexpr int kProvisionalAmFil2Hz = 6000;
inline constexpr int kProvisionalAmFil3Hz = 3000;

// FM / WFM / DV — wide fixed shapes; 15.0 / 10.0 / 7.0 kHz nominal.
inline constexpr int kProvisionalFmFil1Hz = 15000;
inline constexpr int kProvisionalFmFil2Hz = 10000;
inline constexpr int kProvisionalFmFil3Hz = 7000;

// Return the three nominal slot widths (FIL1, FIL2, FIL3) for a mode.
struct SlotWidths {
    int fil1Hz;
    int fil2Hz;
    int fil3Hz;
};

SlotWidths slotWidthsFor(IcomCiv::CivMode mode)
{
    switch (mode) {
    case IcomCiv::CivMode::Lsb:
    case IcomCiv::CivMode::Usb:
        return {kProvisionalSsbFil1Hz, kProvisionalSsbFil2Hz, kProvisionalSsbFil3Hz};
    case IcomCiv::CivMode::Cw:
    case IcomCiv::CivMode::CwR:
        return {kProvisionalCwFil1Hz, kProvisionalCwFil2Hz, kProvisionalCwFil3Hz};
    case IcomCiv::CivMode::Rtty:
    case IcomCiv::CivMode::RttyR:
        return {kProvisionalRttyFil1Hz, kProvisionalRttyFil2Hz, kProvisionalRttyFil3Hz};
    case IcomCiv::CivMode::Am:
        return {kProvisionalAmFil1Hz, kProvisionalAmFil2Hz, kProvisionalAmFil3Hz};
    case IcomCiv::CivMode::Fm:
    case IcomCiv::CivMode::Wfm:
    case IcomCiv::CivMode::Dv:
        return {kProvisionalFmFil1Hz, kProvisionalFmFil2Hz, kProvisionalFmFil3Hz};
    }
    return {kProvisionalSsbFil1Hz, kProvisionalSsbFil2Hz, kProvisionalSsbFil3Hz};
}

} // namespace

IcomBackend::IcomBackend(QObject* parent)
    : RadioBackend(parent)
{
    m_transport = new IcomUdpTransport(this);
    connect(m_transport, &IcomUdpTransport::connected,
            this, &IcomBackend::onTransportConnected);
    connect(m_transport, &IcomUdpTransport::disconnected,
            this, &IcomBackend::onTransportDisconnected);
    connect(m_transport, &IcomUdpTransport::civFrameReceived,
            this, &IcomBackend::onCivFrameReceived);
    connect(m_transport, &IcomUdpTransport::audioPayloadReceived,
            this, &IcomBackend::onAudioPayloadReceived);
    connect(m_transport, &IcomUdpTransport::errorOccurred,
            this, &IcomBackend::errorOccurred);
}

IcomBackend::~IcomBackend() = default;

// --- Pure mapping helpers (no sockets; unit-tested directly) -----------------

IcomCiv::CivMode IcomBackend::civModeFromString(const QString& mode)
{
    const QString m = mode.trimmed().toUpper();
    if (m == QLatin1String("LSB")) {
        return IcomCiv::CivMode::Lsb;
    }
    if (m == QLatin1String("USB")) {
        return IcomCiv::CivMode::Usb;
    }
    if (m == QLatin1String("AM")) {
        return IcomCiv::CivMode::Am;
    }
    if (m == QLatin1String("CW")) {
        return IcomCiv::CivMode::Cw;
    }
    if (m == QLatin1String("CW-R") || m == QLatin1String("CWR")) {
        return IcomCiv::CivMode::CwR;
    }
    if (m == QLatin1String("RTTY")) {
        return IcomCiv::CivMode::Rtty;
    }
    if (m == QLatin1String("RTTY-R") || m == QLatin1String("RTTYR")) {
        return IcomCiv::CivMode::RttyR;
    }
    if (m == QLatin1String("FM")) {
        return IcomCiv::CivMode::Fm;
    }
    if (m == QLatin1String("WFM")) {
        return IcomCiv::CivMode::Wfm;
    }
    if (m == QLatin1String("DV")) {
        return IcomCiv::CivMode::Dv;
    }
    return IcomCiv::CivMode::Usb;
}

QString IcomBackend::civModeToString(IcomCiv::CivMode mode)
{
    switch (mode) {
    case IcomCiv::CivMode::Lsb:
        return QStringLiteral("LSB");
    case IcomCiv::CivMode::Usb:
        return QStringLiteral("USB");
    case IcomCiv::CivMode::Am:
        return QStringLiteral("AM");
    case IcomCiv::CivMode::Cw:
        return QStringLiteral("CW");
    case IcomCiv::CivMode::CwR:
        return QStringLiteral("CW-R");
    case IcomCiv::CivMode::Rtty:
        return QStringLiteral("RTTY");
    case IcomCiv::CivMode::RttyR:
        return QStringLiteral("RTTY-R");
    case IcomCiv::CivMode::Fm:
        return QStringLiteral("FM");
    case IcomCiv::CivMode::Wfm:
        return QStringLiteral("WFM");
    case IcomCiv::CivMode::Dv:
        return QStringLiteral("DV");
    }
    return QStringLiteral("USB");
}

quint8 IcomBackend::filterSlotForBandwidth(IcomCiv::CivMode mode, int bandwidthHz)
{
    // Clamp negatives (a reversed low/high pair) to 0 so the widest slot wins
    // rather than producing a nonsense distance.
    const int requested = bandwidthHz < 0 ? 0 : bandwidthHz;
    const SlotWidths widths = slotWidthsFor(mode);

    // Nearest slot by absolute width distance.  Ties favor the wider slot
    // (FIL1 first), which is the safer default when a request lands exactly
    // between two slots.
    const int d1 = std::abs(requested - widths.fil1Hz);
    const int d2 = std::abs(requested - widths.fil2Hz);
    const int d3 = std::abs(requested - widths.fil3Hz);

    quint8 slot = IcomCiv::kFilterFil1;
    int best = d1;
    if (d2 < best) {
        best = d2;
        slot = IcomCiv::kFilterFil2;
    }
    if (d3 < best) {
        best = d3;
        slot = IcomCiv::kFilterFil3;
    }
    return slot;
}

quint32 IcomBackend::scopeStreamIdForReceiver(int receiver, int scopeCount)
{
    // A radio with two or more scopes routes the sub receiver (index >= 1) to a
    // distinct panadapter stream; anything else collapses onto the main stream.
    if (receiver >= 1 && scopeCount >= 2) {
        return kScopeStreamIdSub;
    }
    return kScopeStreamIdMain;
}

void IcomBackend::setConnectionProfile(const IcomConnectionProfile& profile)
{
    m_profile = profile;
    const IcomModelCaps caps = icomCapsFor(profile.modelKey);
    m_civAddr = caps.defaultCivAddress;
    if (!caps.isKnown) {
        qCWarning(lcIcom).noquote()
            << QStringLiteral("IcomBackend: unknown model '%1'; CI-V address "
                              "unset — set it explicitly before connecting.")
                   .arg(profile.modelKey);
    }
}

void IcomBackend::connectToRadio(const RadioInfo& info)
{
    // The RadioInfo path currently only carries an address; model/username come
    // from the profile set via setConnectionProfile().  Unifying discovery so
    // Icom entries are first-class RadioInfo rows is tracked in #5.
    IcomUdpTransport::ConnectParams params;
    params.address = info.address.isNull() ? m_profile.address : info.address;
    params.controlPort = m_profile.controlPort;
    params.username = m_profile.username;
    params.password = SecretStore::instance().secret(m_profile.id);
    // The device name is echoed by the radio in the serial+audio request; use
    // the model key ("IC-705", "IC-9700", …) so it matches the target radio.
    if (!m_profile.modelKey.trimmed().isEmpty()) {
        params.deviceName = m_profile.modelKey.trimmed();
    }

    if (params.address.isNull()) {
        emit errorOccurred(QStringLiteral("Icom connect: no address"));
        return;
    }
    if (m_civAddr == 0) {
        m_civAddr = icomCapsFor(m_profile.modelKey).defaultCivAddress;
    }
    m_scope.reset();
    m_transport->init();
    m_transport->connectToRadio(params);
}

void IcomBackend::disconnectFromRadio()
{
    m_transport->disconnectFromRadio();
}

bool IcomBackend::selectReceiver(int receiver)
{
    // Main is always the default target; nothing to do for index 0.
    if (receiver <= 0) {
        return true;
    }

    const IcomModelCaps caps = icomCapsFor(m_profile.modelKey);
    if (caps.receiverCount < 2) {
        // Single-RX radio: a sub-receiver does not exist.  No-op with a note
        // rather than sending a select the radio would reject.
        qCInfo(lcIcom).noquote()
            << QStringLiteral("IcomBackend: receiver %1 requested but '%2' is "
                              "single-RX; ignoring sub-receiver select.")
                   .arg(receiver)
                   .arg(m_profile.modelKey);
        return false;
    }

    // Dual-RX main/sub select.  Public CI-V command 0x07 (VFO/receiver select)
    // with sub-command 0xD1 selects the SUB receiver (0xD0 selects MAIN); on
    // radios such as the IC-7610 this is the "Select MAIN/SUB band" control.
    // Marked PROVISIONAL: exact sub-command support varies by model/firmware.
    // The dedicated select command does not exist in IcomCivCodec (owned by
    // another agent), so build the raw frame via encodeFrame() here.
    static constexpr quint8 kCmdSelectVfo = 0x07;      // CI-V 0x07 (public)
    static constexpr quint8 kSubCmdSelectSub = 0xD1;   // 0xD0 = MAIN, 0xD1 = SUB
    const QByteArray selectFrame = IcomCiv::encodeFrame(
        m_civAddr, IcomCiv::kControllerAddress, kCmdSelectVfo,
        QByteArray(1, static_cast<char>(kSubCmdSelectSub)));
    m_transport->sendCivFrame(selectFrame);
    return true;
}

void IcomBackend::setFrequency(int receiver, quint64 hz)
{
    if (m_civAddr == 0) {
        return;
    }
    selectReceiver(receiver);
    m_transport->sendCivFrame(IcomCiv::setFrequency(m_civAddr, hz));
}

void IcomBackend::setMode(int receiver, const QString& mode)
{
    if (m_civAddr == 0) {
        return;
    }
    selectReceiver(receiver);
    m_currentMode = civModeFromString(mode);
    m_transport->sendCivFrame(IcomCiv::setMode(m_civAddr, m_currentMode));
}

void IcomBackend::setFilterBandwidth(int receiver, int lowHz, int highHz)
{
    if (m_civAddr == 0) {
        return;
    }
    selectReceiver(receiver);

    // Icom CI-V exposes discrete filter slots (FIL1/2/3), not arbitrary edges.
    // Quantize the requested audio bandwidth to the nearest slot for the
    // current mode, then re-send mode+filter (CI-V cmd 0x06 carries both), so
    // the mode is preserved while the filter changes.
    const int bandwidthHz = highHz - lowHz;
    const quint8 slot = filterSlotForBandwidth(m_currentMode, bandwidthHz);
    qCInfo(lcIcom).noquote()
        << QStringLiteral("IcomBackend: bandwidth %1 Hz (mode %2) -> filter "
                          "slot FIL%3.")
               .arg(bandwidthHz)
               .arg(civModeToString(m_currentMode))
               .arg(static_cast<int>(slot));
    m_transport->sendCivFrame(IcomCiv::setMode(m_civAddr, m_currentMode, slot));
}

void IcomBackend::refreshState()
{
    if (m_civAddr == 0) {
        return;
    }
    m_transport->sendCivFrame(IcomCiv::readFrequency(m_civAddr));
    m_transport->sendCivFrame(IcomCiv::readMode(m_civAddr));
}

void IcomBackend::onTransportConnected()
{
    m_connected = true;
    emit connected();
    refreshState();
}

void IcomBackend::onTransportDisconnected()
{
    m_connected = false;
    emit disconnected();
}

void IcomBackend::onCivFrameReceived(const QByteArray& civFrame)
{
    const std::optional<IcomCiv::CivFrame> frame = IcomCiv::parseFrame(civFrame);
    if (!frame.has_value()) {
        return;
    }
    switch (frame->cmd) {
    case 0x00:  // transceive frequency push
    case IcomCiv::kCmdReadFrequency:
    case IcomCiv::kCmdSetFrequency: {
        const std::optional<quint64> hz = IcomCiv::parseFrequencyResponse(*frame);
        if (hz.has_value()) {
            handleFrequency(*hz);
        }
        break;
    }
    case 0x01:  // transceive mode push
    case IcomCiv::kCmdReadMode:
    case IcomCiv::kCmdSetMode: {
        const std::optional<IcomCiv::CivModeResponse> m =
            IcomCiv::parseModeResponse(*frame);
        if (m.has_value()) {
            handleMode(m->mode, m->hasFilter ? m->filter : 0);
        }
        break;
    }
    case IcomCiv::kCmdScopeWaveform:
        handleScope(frame->payload);
        break;
    default:
        break;
    }
}

void IcomBackend::onAudioPayloadReceived(const QByteArray& payload)
{
    const QByteArray floatMono =
        IcomAudio::decodeToFloatMono(payload, m_audioFormat);
    if (!floatMono.isEmpty()) {
        emit audioDataReady(floatMono);
    }
}

void IcomBackend::handleFrequency(quint64 hz)
{
    // Normalized to the same object/key shape RadioModel already parses from
    // Flex S-lines (RF_frequency in MHz).  The exact schema is finalized when
    // RadioModel consumes the RadioBackend seam (#2 follow-up).
    QMap<QString, QString> kvs;
    kvs.insert(QStringLiteral("RF_frequency"),
               QString::number(static_cast<double>(hz) / 1.0e6, 'f', 6));
    emit statusReceived(QStringLiteral("slice 0"), kvs);
}

void IcomBackend::handleMode(IcomCiv::CivMode mode, int filter)
{
    // Track the radio's reported mode so setFilterBandwidth re-sends the right
    // mode+filter pair.
    m_currentMode = mode;
    QMap<QString, QString> kvs;
    kvs.insert(QStringLiteral("mode"), civModeToString(mode));
    if (filter > 0) {
        kvs.insert(QStringLiteral("filter"), QString::number(filter));
    }
    emit statusReceived(QStringLiteral("slice 0"), kvs);
}

void IcomBackend::handleScope(const QByteArray& civ27Payload)
{
    if (!m_scope.ingest(civ27Payload)) {
        return;
    }
    if (!m_scope.complete()) {
        return;
    }
    const IcomScope::ScopeGeometry geom =
        IcomScope::scopeGeometry(m_scope.header());
    const QVector<float> bins =
        IcomScope::samplesToDbm(m_scope.rawSamples(), m_scopeRefDbm, m_scopeRangeDb);
    const qint64 ns = nowNs();
    // Feed both the spectrum trace and a waterfall row, as the VITA-49 path
    // does; PanadapterModel consumes these identically regardless of source.
    // v1 scope stream is the main receiver's panadapter.  Sub-RX scope frames
    // (dual-RX radios) would be demuxed to kScopeStreamIdSub once the transport
    // tags scope payloads with their originating receiver; until then a single
    // 0x27 stream maps to the main panadapter.
    emit spectrumReady(kScopeStreamIdMain, bins, ns);
    emit waterfallRowReady(kScopeStreamIdMain, bins, geom.lowMhz, geom.highMhz,
                           /*timecode=*/0, ns);
    m_scope.reset();
}

} // namespace AetherSDR
