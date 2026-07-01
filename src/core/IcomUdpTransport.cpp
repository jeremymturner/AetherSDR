#include "IcomUdpTransport.h"

#include "IcomSessionCodec.h"
#include "LogManager.h"

#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>

#include <optional>

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
    if (m_areYouThereTimer == nullptr) {
        m_areYouThereTimer = new QTimer(this);
        m_areYouThereTimer->setInterval(kAreYouThereMs);
        connect(m_areYouThereTimer, &QTimer::timeout,
                this, &IcomUdpTransport::sendAreYouThere);
    }
}

void IcomUdpTransport::connectToRadio(const ConnectParams& params)
{
    if (m_controlSocket == nullptr) {
        init();
    }
    m_params = params;
    m_txSeq = 0;
    // The radio identifies our session by a random 32-bit id we choose and
    // stamp into every outbound packet's `sentid`.  (Derived behaviour: the
    // client picks its own id and the radio echoes it back as `rcvdid`.)
    m_localId = QRandomGenerator::global()->generate();
    m_remoteId = 0;
    m_civStreamSeq = 0;
    m_audioStreamSeq = 0;
    m_civRxSeq = -1;
    m_audioRxSeq = -1;
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

    // Open the session: fire the first "are you there" and keep retrying on a
    // timer until the radio answers "I am here" (handled in
    // handleControlDatagram()).  Keepalive starts once we reach Connected.
    sendAreYouThere();
    if (m_areYouThereTimer != nullptr) {
        m_areYouThereTimer->start();
    }
}

void IcomUdpTransport::disconnectFromRadio()
{
    if (m_state == State::Disconnected) {
        return;
    }
    // Send the clean disconnect so the radio frees its single network session
    // (a dangling session locks out the next client).  Only meaningful once we
    // have a remote id to address it to.
    if (m_controlSocket != nullptr && m_remoteId != 0) {
        const QByteArray bye = IcomSession::encodeDisconnect(
            nextControlSeq(), m_localId, m_remoteId);
        m_controlSocket->writeDatagram(bye, m_params.address,
                                       m_params.controlPort);
    }
    if (m_keepalive != nullptr) {
        m_keepalive->stop();
    }
    if (m_areYouThereTimer != nullptr) {
        m_areYouThereTimer->stop();
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
        return;  // nothing to send
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
    if (m_controlSocket == nullptr) {
        return;
    }
    if (m_state != State::Connected && m_state != State::Authenticating) {
        return;
    }
    // Periodic idle keepalive on the control port.  An idle packet (opcode 0)
    // carrying our current sequence keeps the radio's single session alive;
    // dropping it makes the radio time the session out.  Cadence
    // (kKeepaliveIntervalMs) is PROVISIONAL pending hardware capture.
    const QByteArray idle = IcomSession::encodeIdle(nextControlSeq(),
                                                    m_localId, m_remoteId);
    m_controlSocket->writeDatagram(idle, m_params.address,
                                   m_params.controlPort);
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
    if (m_areYouThereTimer != nullptr) {
        m_areYouThereTimer->stop();
    }
    // QObject parent-ownership deletes the sockets/timer; just drop the state.
    m_state = State::Disconnected;
}

// ---------------------------------------------------------------------------
// Session codec seams.  These four functions are the only place the transport
// touches the wire format; each delegates to IcomSessionCodec (the pure,
// testable framing codec derived clean-room from open-source references — see
// IcomSessionCodec.h for provenance).  Never derive from the proprietary
// RS-BA1 binary.  All offsets/opcodes are PROVISIONAL until hardware capture.
// ---------------------------------------------------------------------------

quint16 IcomUdpTransport::nextControlSeq()
{
    return m_txSeq++;
}

void IcomUdpTransport::sendAreYouThere()
{
    if (m_controlSocket == nullptr) {
        return;
    }
    const QByteArray opener =
        IcomSession::encodeAreYouThere(nextControlSeq(), m_localId);
    m_controlSocket->writeDatagram(opener, m_params.address,
                                   m_params.controlPort);
}

QByteArray IcomUdpTransport::buildControlPacket(quint8 opcode,
                                                const QByteArray& body)
{
    // Retained for API compatibility with the original scaffolding.  Bodyless
    // control packets are the common case; a non-empty body is not used by the
    // fixed control opcodes, so it is ignored here (stream bodies go through
    // wrapStreamPayload()).
    Q_UNUSED(body);
    return IcomSession::encodeControl(opcode, nextControlSeq(),
                                      m_localId, m_remoteId);
}

void IcomUdpTransport::handleControlDatagram(const QByteArray& datagram)
{
    const std::optional<IcomSession::ParsedControl> parsed =
        IcomSession::parseControl(datagram);
    if (!parsed) {
        return;  // malformed / too short — parseControl already bounds-checked
    }

    switch (parsed->kind) {
    case IcomSession::ControlKind::IAmHere: {
        // The radio answered our opener.  Latch its session id (its `sentid`)
        // as our remote id, stop retrying the opener, and complete the
        // handshake.  Older/local sessions need no token, so we transition
        // straight to Connected; the token/login path is a marked seam below.
        m_remoteId = parsed->header.sentId;
        if (m_areYouThereTimer != nullptr) {
            m_areYouThereTimer->stop();
        }
        qCInfo(lcIcom) << "Icom transport: 'I am here' received, remoteId="
                       << Qt::hex << m_remoteId;

        // Acknowledge readiness (second leg of the handshake).
        if (m_controlSocket != nullptr) {
            const QByteArray ready = IcomSession::encodeAreYouReady(
                nextControlSeq(), m_localId, m_remoteId);
            m_controlSocket->writeDatagram(ready, m_params.address,
                                           m_params.controlPort);
        }

        // TODO(cleanroom #10 / auth): token-based login (username/password)
        // for authenticated remote sessions is not yet derived with enough
        // confidence to implement.  For un-authenticated (older firmware /
        // local LAN) sessions no token is required, so we go live now.  When
        // the token exchange is confirmed, gate this on m_params having
        // credentials and route through State::Authenticating first.
        setState(State::Connected);
        if (m_keepalive != nullptr) {
            m_keepalive->start();
        }
        break;
    }
    case IcomSession::ControlKind::AreYouReady: {
        // Peer's readiness ack.  If we somehow are still Connecting, this also
        // gets us live; otherwise it is informational.
        if (m_state == State::Connecting || m_state == State::Authenticating) {
            setState(State::Connected);
            if (m_keepalive != nullptr) {
                m_keepalive->start();
            }
        }
        break;
    }
    case IcomSession::ControlKind::Ping: {
        // Answer a ping request by echoing its sequence + timestamp with the
        // reply flag set.  (We ignore our own ping responses, reply == 1.)
        if (parsed->pingReply == 0 && m_controlSocket != nullptr) {
            const QByteArray pong = IcomSession::encodePing(
                parsed->header.seq, m_localId, m_remoteId,
                /*reply=*/1, parsed->pingTime);
            m_controlSocket->writeDatagram(pong, m_params.address,
                                           m_params.controlPort);
        }
        break;
    }
    case IcomSession::ControlKind::Disconnect: {
        // The radio tore the session down on its side.
        if (m_state != State::Disconnected) {
            if (m_keepalive != nullptr) {
                m_keepalive->stop();
            }
            if (m_areYouThereTimer != nullptr) {
                m_areYouThereTimer->stop();
            }
            setState(State::Disconnected);
            emit disconnected();
        }
        break;
    }
    case IcomSession::ControlKind::Retransmit:
        // The radio wants control packets resent.  A full retransmit buffer is
        // future work (issue #10); log the request so gaps are observable.
        qCDebug(lcIcom) << "Icom transport: retransmit requested for"
                        << parsed->retransmitSeqs.size() << "packet(s)";
        break;
    case IcomSession::ControlKind::Idle:
    case IcomSession::ControlKind::Unknown:
    default:
        break;
    }
}

QByteArray IcomUdpTransport::wrapStreamPayload(quint16 port,
                                               const QByteArray& payload)
{
    // Each stream carries its own big-endian sub-sequence counter, distinct
    // from the control-port sequence.
    quint16 streamSeq = 0;
    if (port == kAudioPort) {
        streamSeq = m_audioStreamSeq++;
    } else {
        streamSeq = m_civStreamSeq++;
    }
    return IcomSession::encodeStreamPayload(nextControlSeq(), streamSeq,
                                            m_localId, m_remoteId, payload);
}

QByteArray IcomUdpTransport::unwrapStreamPayload(quint16 port,
                                                 const QByteArray& datagram,
                                                 bool* ok)
{
    const std::optional<IcomSession::ParsedStream> parsed =
        IcomSession::parseStreamPayload(datagram);
    if (!parsed || !parsed->valid || parsed->payload.isEmpty()) {
        if (ok != nullptr) {
            *ok = false;
        }
        return QByteArray();
    }
    if (ok != nullptr) {
        *ok = true;
    }
    return trackStreamSequence(port, parsed->innerSeq, parsed->payload);
}

QByteArray IcomUdpTransport::trackStreamSequence(quint16 port, quint16 seq,
                                                 const QByteArray& payload)
{
    int* lastSeq = (port == kAudioPort) ? &m_audioRxSeq : &m_civRxSeq;
    quint64* gaps = (port == kAudioPort) ? &m_audioSeqGaps : &m_civSeqGaps;

    if (*lastSeq >= 0) {
        const quint16 expected = static_cast<quint16>(*lastSeq) + 1;
        if (seq != expected) {
            // A gap (or reorder).  Count the number of missing sequence steps
            // forward; ignore backwards/duplicate deliveries.
            const quint16 delta = static_cast<quint16>(seq - expected);
            if (delta > 0 && delta < 0x8000) {  // forward gap, not a wrap-back
                *gaps += delta;
                emit sequenceGap(port, *gaps);
            }
        }
    }
    *lastSeq = seq;
    return payload;
}

} // namespace AetherSDR
