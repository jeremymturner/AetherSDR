#include "IcomUdpTransport.h"

#include "LogManager.h"

#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace AetherSDR {

IcomUdpTransport::IcomUdpTransport(QObject* parent)
    : QObject(parent)
{
}

IcomUdpTransport::~IcomUdpTransport()
{
    teardownSockets();
}

void IcomUdpTransport::init()
{
    // Created lazily on the worker thread so socket affinity is correct (#502).
    if (m_controlSocket == nullptr) {
        m_controlSocket = new QUdpSocket(this);
        connect(m_controlSocket, &QUdpSocket::readyRead,
                this, &IcomUdpTransport::onControlReadyRead);
    }
    if (m_civSocket == nullptr) {
        m_civSocket = new QUdpSocket(this);
        connect(m_civSocket, &QUdpSocket::readyRead,
                this, &IcomUdpTransport::onCivReadyRead);
    }
    if (m_audioSocket == nullptr) {
        m_audioSocket = new QUdpSocket(this);
        connect(m_audioSocket, &QUdpSocket::readyRead,
                this, &IcomUdpTransport::onAudioReadyRead);
    }
    if (m_keepalive == nullptr) {
        m_keepalive = new QTimer(this);
        m_keepalive->setInterval(kKeepaliveIntervalMs);
        connect(m_keepalive, &QTimer::timeout,
                this, &IcomUdpTransport::onKeepaliveTick);
    }
}

void IcomUdpTransport::connectToRadio(const ConnectParams& params)
{
    if (m_controlSocket == nullptr) {
        init();
    }
    m_params = params;
    m_txSeq = 0;
    m_localId = 0;
    m_remoteId = 0;
    m_civSeqGaps = 0;
    m_audioSeqGaps = 0;

    // Bind each UDP socket to an ephemeral local port; the radio replies to
    // whatever source port we send from.  Icom's fixed destination ports
    // (50001/50002/50003) must not be remapped.
    const bool cOk = m_controlSocket->bind(QHostAddress::AnyIPv4, 0);
    const bool vOk = m_civSocket->bind(QHostAddress::AnyIPv4, 0);
    const bool aOk = m_audioSocket->bind(QHostAddress::AnyIPv4, 0);
    if (!cOk || !vOk || !aOk) {
        qCWarning(lcIcom) << "Icom transport: UDP bind failed"
                          << "control" << cOk << "civ" << vOk << "audio" << aOk;
        setState(State::Error);
        emit errorOccurred(QStringLiteral("Failed to bind Icom UDP sockets"));
        return;
    }

    setState(State::Connecting);
    qCInfo(lcIcom).noquote()
        << QStringLiteral("Icom transport: connecting to %1:%2 (user '%3')")
               .arg(m_params.address.toString())
               .arg(m_params.controlPort)
               .arg(m_params.username);

    // TODO(cleanroom #10): send the real control-port "are you there" opener
    // and drive the handshake state machine from handleControlDatagram().  The
    // opcode/body encoding is derived from captures and/or wfview's open-source
    // code, then written as our own (read-and-reimplement; no verbatim copy).
    // Until buildControlPacket() is filled in, the session cannot complete and
    // will remain in Connecting; this is intentional — the scaffolding is here
    // for review, the proprietary encoding is the one remaining seam.
    const QByteArray opener = buildControlPacket(/*opcode=*/0x00, QByteArray());
    if (!opener.isEmpty()) {
        m_controlSocket->writeDatagram(opener, m_params.address,
                                       m_params.controlPort);
        m_keepalive->start();
    }
}

void IcomUdpTransport::disconnectFromRadio()
{
    if (m_state == State::Disconnected) {
        return;
    }
    // TODO(cleanroom #10): send the clean disconnect/idle-close control packet
    // so the radio frees its single network session (a dangling session locks
    // out the next client).  Encoding from capture.
    if (m_controlSocket != nullptr && m_state == State::Connected) {
        const QByteArray bye = buildControlPacket(/*opcode=*/0xFF, QByteArray());
        if (!bye.isEmpty()) {
            m_controlSocket->writeDatagram(bye, m_params.address,
                                           m_params.controlPort);
        }
    }
    if (m_keepalive != nullptr) {
        m_keepalive->stop();
    }
    setState(State::Disconnected);
    emit disconnected();
}

void IcomUdpTransport::sendCivFrame(const QByteArray& civFrame)
{
    if (m_state != State::Connected || m_civSocket == nullptr) {
        qCWarning(lcIcom) << "Icom transport: sendCivFrame while not connected";
        return;
    }
    const QByteArray wrapped = wrapStreamPayload(kCivPort, civFrame);
    if (wrapped.isEmpty()) {
        return;  // seam not implemented yet
    }
    m_civSocket->writeDatagram(wrapped, m_params.address, kCivPort);
}

void IcomUdpTransport::sendAudioPayload(const QByteArray& payload)
{
    if (m_state != State::Connected || m_audioSocket == nullptr) {
        return;
    }
    const QByteArray wrapped = wrapStreamPayload(kAudioPort, payload);
    if (wrapped.isEmpty()) {
        return;
    }
    m_audioSocket->writeDatagram(wrapped, m_params.address, kAudioPort);
}

void IcomUdpTransport::onControlReadyRead()
{
    while (m_controlSocket != nullptr && m_controlSocket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_controlSocket->receiveDatagram();
        handleControlDatagram(dg.data());
    }
}

void IcomUdpTransport::onCivReadyRead()
{
    while (m_civSocket != nullptr && m_civSocket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_civSocket->receiveDatagram();
        bool ok = false;
        const QByteArray civFrame = unwrapStreamPayload(kCivPort, dg.data(), &ok);
        if (ok && !civFrame.isEmpty()) {
            emit civFrameReceived(civFrame);
        }
    }
}

void IcomUdpTransport::onAudioReadyRead()
{
    while (m_audioSocket != nullptr && m_audioSocket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_audioSocket->receiveDatagram();
        bool ok = false;
        const QByteArray payload = unwrapStreamPayload(kAudioPort, dg.data(), &ok);
        if (ok && !payload.isEmpty()) {
            emit audioPayloadReceived(payload);
        }
    }
}

void IcomUdpTransport::onKeepaliveTick()
{
    if (m_state != State::Connected && m_state != State::Authenticating) {
        return;
    }
    // TODO(cleanroom #10): emit the periodic idle/ping keepalive on the control
    // port.  Cadence (kKeepaliveIntervalMs) is provisional pending capture.
    const QByteArray ping = buildControlPacket(/*opcode=*/0x01, QByteArray());
    if (!ping.isEmpty() && m_controlSocket != nullptr) {
        m_controlSocket->writeDatagram(ping, m_params.address,
                                       m_params.controlPort);
    }
}

void IcomUdpTransport::setState(State s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
    if (s == State::Connected) {
        emit connected();
    }
}

void IcomUdpTransport::teardownSockets()
{
    if (m_keepalive != nullptr) {
        m_keepalive->stop();
    }
    // QObject parent-ownership deletes the sockets/timer; just drop the state.
    m_state = State::Disconnected;
}

// ---------------------------------------------------------------------------
// Session codec seams.  These are the ONLY four functions that encode/decode
// the Icom session wire format.  They are documented no-ops until the format is
// derived from clean sources — first-party captures and/or reading wfview's
// open-source code (a permitted reference under Principle IV) — and then
// implemented as our OWN code (read-and-reimplement, no verbatim copy).  See
// docs/icom-cleanroom-design.md (issue #10).  Never derive from the proprietary
// RS-BA1 binary.
// ---------------------------------------------------------------------------

QByteArray IcomUdpTransport::buildControlPacket(quint8 opcode,
                                                const QByteArray& body)
{
    Q_UNUSED(opcode);
    Q_UNUSED(body);
    // TODO(cleanroom #10): assemble the control-port packet (local/remote id,
    // sequence, opcode, body) from the captured layout.
    return QByteArray();
}

void IcomUdpTransport::handleControlDatagram(const QByteArray& datagram)
{
    Q_UNUSED(datagram);
    // TODO(cleanroom #10): parse control replies, advance the handshake
    // (Connecting -> Authenticating -> Connected), latch m_remoteId, feed the
    // retransmit tracker, and call setState() accordingly.  Boundary-validate
    // every field before indexing (Constitution: boundary input validation).
}

QByteArray IcomUdpTransport::wrapStreamPayload(quint16 port,
                                               const QByteArray& payload)
{
    Q_UNUSED(port);
    Q_UNUSED(payload);
    // TODO(cleanroom #10): wrap a CI-V frame / audio payload in the 50002/50003
    // session envelope (ids + sequence).  Returns empty until implemented.
    return QByteArray();
}

QByteArray IcomUdpTransport::unwrapStreamPayload(quint16 port,
                                                 const QByteArray& datagram,
                                                 bool* ok)
{
    Q_UNUSED(port);
    Q_UNUSED(datagram);
    // TODO(cleanroom #10): strip the session envelope, validate sequence (emit
    // sequenceGap on loss), and return the inner CI-V frame / audio payload.
    // Boundary-validate length before indexing.
    if (ok != nullptr) {
        *ok = false;
    }
    return QByteArray();
}

} // namespace AetherSDR
