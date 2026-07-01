#include "FlexRadioBackend.h"

#include "LogManager.h"
#include "RadioConnection.h"

namespace AetherSDR {

FlexRadioBackend::FlexRadioBackend(QObject* parent)
    : RadioBackend(parent)
{
    m_conn = new RadioConnection(this);

    // Forward the connection's lifecycle + status straight through to the
    // RadioBackend signals.  Flex status is already the normalized
    // (object, key/value) shape the interface documents, so `statusReceived`
    // is forwarded verbatim — no translation, unlike the Icom backend.
    connect(m_conn, &RadioConnection::connected,
            this, &RadioBackend::connected);
    connect(m_conn, &RadioConnection::disconnected,
            this, &RadioBackend::disconnected);
    connect(m_conn, &RadioConnection::statusReceived,
            this, &RadioBackend::statusReceived);
    connect(m_conn, &RadioConnection::errorOccurred,
            this, &RadioBackend::errorOccurred);
}

FlexRadioBackend::~FlexRadioBackend() = default;

bool FlexRadioBackend::isConnected() const
{
    return m_conn != nullptr && m_conn->isConnected();
}

quint32 FlexRadioBackend::nextSeq()
{
    return m_seqCounter.fetch_add(1);
}

void FlexRadioBackend::connectToRadio(const RadioInfo& info)
{
    // Capture a human-facing identity for displayName().  Prefer the operator's
    // nickname, then the model, then the discovery name.
    if (!info.nickname.trimmed().isEmpty()) {
        m_displayName = info.nickname.trimmed();
    } else if (!info.model.trimmed().isEmpty()) {
        m_displayName = info.model.trimmed();
    } else {
        m_displayName = info.name.trimmed();
    }

    // The RadioConnection creates its socket/timer on first connect (it must run
    // on the worker thread after moveToThread()); we simply delegate.
    m_conn->init();
    m_conn->connectToRadio(info);
}

void FlexRadioBackend::disconnectFromRadio()
{
    m_conn->disconnectFromRadio();
}

void FlexRadioBackend::setFrequency(int receiver, quint64 hz)
{
    // SmartSDR: "slice tune <id> <freq_mhz> autopan=0".  autopan=0 keeps the
    // radio from recentering the pan on a bare frequency change (#292), matching
    // SliceModel::setFrequency.  Frequency is expressed in MHz with 6 decimals
    // (1 Hz resolution), as the SmartSDR pcap dialect requires.
    const double mhz = static_cast<double>(hz) / 1.0e6;
    const QString cmd =
        QStringLiteral("slice tune %1 %2 autopan=0")
            .arg(receiver)
            .arg(mhz, 0, 'f', 6);
    m_conn->writeCommand(nextSeq(), cmd);
}

void FlexRadioBackend::setMode(int receiver, const QString& mode)
{
    // SmartSDR: "slice set <id> mode=<MODE>".  Flex mode tokens (USB/LSB/CW/…)
    // are passed through as-is, matching SliceModel::setMode.
    const QString cmd =
        QStringLiteral("slice set %1 mode=%2").arg(receiver).arg(mode);
    m_conn->writeCommand(nextSeq(), cmd);
}

void FlexRadioBackend::setFilterBandwidth(int receiver, int lowHz, int highHz)
{
    // SmartSDR: "filt <id> <low_hz> <high_hz>" (edges in Hz), matching
    // SliceModel::setFilterWidth.
    const QString cmd =
        QStringLiteral("filt %1 %2 %3").arg(receiver).arg(lowHz).arg(highHz);
    m_conn->writeCommand(nextSeq(), cmd);
}

void FlexRadioBackend::refreshState()
{
    // Flex pushes state automatically once subscribed: RadioModel issues
    // "sub slice all" / "sub pan all" / … at connect, after which the radio
    // streams status (S-lines) unprompted.  Re-issuing "sub slice all" here is
    // the natural "request a fresh push" for the RX-first control set (slice
    // freq/mode/filter live under the slice object).  When RadioModel adopts
    // this seam (#2 follow-up), the full subscription fan-out belongs here.
    if (!isConnected()) {
        qCWarning(lcConnection)
            << "FlexRadioBackend::refreshState called while disconnected — "
               "ignoring.";
        return;
    }
    m_conn->writeCommand(nextSeq(), QStringLiteral("sub slice all"));
}

} // namespace AetherSDR
