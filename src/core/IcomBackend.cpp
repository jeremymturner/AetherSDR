#include "IcomBackend.h"

#include "IcomRadioCapabilities.h"
#include "LogManager.h"
#include "RadioDiscovery.h"
#include "SecretStore.h"

#include <QDateTime>

namespace AetherSDR {

namespace {

IcomCiv::CivMode modeFromString(const QString& mode)
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

QString modeToString(IcomCiv::CivMode mode)
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

qint64 nowNs()
{
    return QDateTime::currentMSecsSinceEpoch() * 1000000LL;
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

void IcomBackend::setFrequency(int receiver, quint64 hz)
{
    Q_UNUSED(receiver);  // dual-RX addressing: #9 follow-up
    if (m_civAddr == 0) {
        return;
    }
    m_transport->sendCivFrame(IcomCiv::setFrequency(m_civAddr, hz));
}

void IcomBackend::setMode(int receiver, const QString& mode)
{
    Q_UNUSED(receiver);
    if (m_civAddr == 0) {
        return;
    }
    m_transport->sendCivFrame(IcomCiv::setMode(m_civAddr, modeFromString(mode)));
}

void IcomBackend::setFilterBandwidth(int receiver, int lowHz, int highHz)
{
    Q_UNUSED(receiver);
    Q_UNUSED(lowHz);
    Q_UNUSED(highHz);
    // Icom CI-V exposes discrete filter slots (FIL1/2/3), not arbitrary edges.
    // Mapping a requested bandwidth to the nearest slot is deferred to the
    // capabilities work (#9); log so the no-op is visible.
    qCInfo(lcIcom) << "IcomBackend: setFilterBandwidth ignored — Icom uses "
                      "discrete filter slots (see #9).";
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
    QMap<QString, QString> kvs;
    kvs.insert(QStringLiteral("mode"), modeToString(mode));
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
    emit spectrumReady(kScopeStreamId, bins, ns);
    emit waterfallRowReady(kScopeStreamId, bins, geom.lowMhz, geom.highMhz,
                           /*timecode=*/0, ns);
    m_scope.reset();
}

} // namespace AetherSDR
