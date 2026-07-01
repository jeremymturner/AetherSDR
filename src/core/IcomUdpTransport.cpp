#include "IcomUdpTransport.h"

#include "IcomSessionCodec.h"  // passcode()
#include "LogManager.h"

#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>
#include <QtEndian>

#include <cstring>

// ---------------------------------------------------------------------------
// Full authenticated Icom LAN client.  See IcomUdpTransport.h for provenance.
// The packet layouts and the login/token/conninfo/stream-open flow are a
// clean-room re-implementation of the DOCUMENTED open-source references
// (kappanhang, which ships verbatim IC-705 captures, and wfview's
// packettypes.h).  Byte offsets and literal magic values below are annotated
// with the reference field they correspond to.  Session ids are big-endian on
// the wire; len/type/seq are little-endian.
// ---------------------------------------------------------------------------

namespace AetherSDR {

namespace {

// Little-endian byte helpers into a QByteArray already sized to hold the field.
void putLe16(QByteArray& b, int off, quint16 v)
{
    b[off]     = static_cast<char>(v & 0xff);
    b[off + 1] = static_cast<char>((v >> 8) & 0xff);
}
void putLe32(QByteArray& b, int off, quint32 v)
{
    b[off]     = static_cast<char>(v & 0xff);
    b[off + 1] = static_cast<char>((v >> 8) & 0xff);
    b[off + 2] = static_cast<char>((v >> 16) & 0xff);
    b[off + 3] = static_cast<char>((v >> 24) & 0xff);
}
void putBe32(QByteArray& b, int off, quint32 v)
{
    b[off]     = static_cast<char>((v >> 24) & 0xff);
    b[off + 1] = static_cast<char>((v >> 16) & 0xff);
    b[off + 2] = static_cast<char>((v >> 8) & 0xff);
    b[off + 3] = static_cast<char>(v & 0xff);
}
quint16 getLe16(const QByteArray& b, int off)
{
    return static_cast<quint16>(static_cast<quint8>(b[off]))
         | (static_cast<quint16>(static_cast<quint8>(b[off + 1])) << 8);
}
quint32 getBe32(const QByteArray& b, int off)
{
    return (static_cast<quint32>(static_cast<quint8>(b[off])) << 24)
         | (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16)
         | (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 8)
         | (static_cast<quint32>(static_cast<quint8>(b[off + 3])));
}

// First bytes of a datagram equal to `prefix`?
bool startsWith(const QByteArray& b, std::initializer_list<quint8> prefix)
{
    if (b.size() < static_cast<int>(prefix.size())) {
        return false;
    }
    int i = 0;
    for (quint8 want : prefix) {
        if (static_cast<quint8>(b[i++]) != want) {
            return false;
        }
    }
    return true;
}

QString hexPreview(const QByteArray& b, int maxBytes = 32)
{
    const int n = qMin(b.size(), maxBytes);
    QString s;
    s.reserve(n * 3);
    for (int i = 0; i < n; ++i) {
        s += QString::asprintf("%02x ", static_cast<quint8>(b[i]));
    }
    if (b.size() > maxBytes) {
        s += QStringLiteral("… (%1 bytes)").arg(b.size());
    }
    return s.trimmed();
}

const char* streamName(int id)  // StreamId as int to avoid header coupling here
{
    switch (id) {
    case 0: return "control";
    case 1: return "civ";
    case 2: return "audio";
    default: return "?";
    }
}

// Retransmit buffer depth: keep the last N tracked packets around so we can
// answer the radio's retransmit requests.
constexpr int kTxBufDepth = 256;

// Whether to hex-log a datagram.  We log the control-plane (handshake, login,
// auth, conninfo, CI-V) but skip the high-rate bulk that would flood the trace
// once connected: audio frames (>=256 bytes) and the 100 ms idle keepalive
// (16-byte type 0x00).  Ping (21 bytes, every 3 s) is cheap enough to keep.
bool shouldLog(const QByteArray& dg)
{
    if (dg.size() >= 256) {
        return false;
    }
    if (dg.size() == 0x10
        && static_cast<quint8>(dg[4]) == 0x00
        && static_cast<quint8>(dg[5]) == 0x00) {
        return false;  // idle keepalive
    }
    return true;
}

} // namespace

IcomUdpTransport::IcomUdpTransport(QObject* parent)
    : QObject(parent)
{
    m_control.port = kControlPort;
    m_civ.port     = kCivPort;
    m_audio.port   = kAudioPort;
}

IcomUdpTransport::~IcomUdpTransport()
{
    teardown();
}

IcomUdpTransport::Stream& IcomUdpTransport::streamFor(StreamId id)
{
    switch (id) {
    case StreamId::Control: return m_control;
    case StreamId::Civ:     return m_civ;
    case StreamId::Audio:   return m_audio;
    }
    return m_control;
}

IcomUdpTransport::Stream* IcomUdpTransport::streamForSocket(QObject* sock)
{
    if (sock == m_control.sock) return &m_control;
    if (sock == m_civ.sock)     return &m_civ;
    if (sock == m_audio.sock)   return &m_audio;
    return nullptr;
}

void IcomUdpTransport::init()
{
    auto makeSocket = [this](Stream& s, void (IcomUdpTransport::*slot)()) {
        if (s.sock == nullptr) {
            s.sock = new QUdpSocket(this);
            // Enlarge the OS receive buffer so bursty 48 kHz audio isn't dropped
            // when the event loop is briefly busy.
            s.sock->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                                    1 << 20);
            connect(s.sock, &QUdpSocket::readyRead, this, slot);
        }
        if (s.idleTimer == nullptr) {
            s.idleTimer = new QTimer(this);
            s.idleTimer->setInterval(kIdleIntervalMs);
        }
        if (s.pingTimer == nullptr) {
            s.pingTimer = new QTimer(this);
            s.pingTimer->setInterval(kPingIntervalMs);
        }
    };
    makeSocket(m_control, &IcomUdpTransport::onControlReadyRead);
    makeSocket(m_civ,     &IcomUdpTransport::onCivReadyRead);
    makeSocket(m_audio,   &IcomUdpTransport::onAudioReadyRead);

    connect(m_control.idleTimer, &QTimer::timeout, this, [this] { sendIdle(StreamId::Control); });
    connect(m_civ.idleTimer,     &QTimer::timeout, this, [this] { sendIdle(StreamId::Civ); });
    connect(m_control.pingTimer, &QTimer::timeout, this, [this] { sendPingRequest(StreamId::Control); });
    connect(m_civ.pingTimer,     &QTimer::timeout, this, [this] { sendPingRequest(StreamId::Civ); });
    connect(m_audio.pingTimer,   &QTimer::timeout, this, [this] { sendPingRequest(StreamId::Audio); });

    if (m_reauthTimer == nullptr) {
        m_reauthTimer = new QTimer(this);
        m_reauthTimer->setInterval(kReauthIntervalMs);
        connect(m_reauthTimer, &QTimer::timeout, this, [this] {
            if (m_gotAuthId) {
                qCDebug(lcIcom) << "Icom: periodic re-auth";
                sendAuth(0x05);
            }
        });
    }
    if (m_connectTimeout == nullptr) {
        m_connectTimeout = new QTimer(this);
        m_connectTimeout->setSingleShot(true);
        m_connectTimeout->setInterval(kConnectTimeoutMs);
        connect(m_connectTimeout, &QTimer::timeout, this, [this] {
            if (m_state != State::Connected) {
                fail(QStringLiteral(
                    "The Icom radio did not complete login within %1 s. Check the IP "
                    "address and the network username/password, and that the radio's "
                    "network control is enabled.").arg(kConnectTimeoutMs / 1000));
            }
        });
    }
}

void IcomUdpTransport::connectToRadio(const ConnectParams& params)
{
    if (m_control.sock == nullptr) {
        init();
    }
    disconnectFromRadio();  // clean any prior session

    m_params = params;

    // Reset all per-stream + auth state.
    auto resetStream = [](Stream& s) {
        s.localSID = QRandomGenerator::global()->generate();
        s.remoteSID = 0;
        s.gotRemoteSID = false;
        s.ready = false;
        s.txSeq = 1;
        s.pingSeq = 0;
        s.pingInnerSeq = 0x8304;
        s.streamSeq = 0;
        s.txBuf.clear();
        s.lastRxSeq = -1;
        s.seqGaps = 0;
    };
    resetStream(m_control);
    resetStream(m_civ);
    resetStream(m_audio);
    m_authInnerSeq = 0;
    std::memset(m_authId, 0, sizeof(m_authId));
    m_gotAuthId = false;
    std::memset(m_a8ReplyId, 0, sizeof(m_a8ReplyId));
    m_gotA8ReplyId = false;
    m_authOk = false;
    m_streamsRequested = false;
    m_loginResponseSeen = false;
    m_pendingCiv.clear();

    // Bind the control socket to an ephemeral local port.  CI-V/audio sockets
    // are bound when their streams are opened after login.
    if (m_control.sock->state() != QAbstractSocket::BoundState
        && !m_control.sock->bind(QHostAddress::AnyIPv4, 0)) {
        fail(QStringLiteral("Failed to bind Icom control socket: %1")
                 .arg(m_control.sock->errorString()));
        return;
    }

    setState(State::Connecting);
    qCInfo(lcIcom).noquote()
        << QStringLiteral("Icom: connecting to %1:%2 as user '%3'")
               .arg(m_params.address.toString())
               .arg(m_params.controlPort)
               .arg(m_params.username);

    m_connectTimeout->start();
    startStreamHandshake(StreamId::Control);
}

void IcomUdpTransport::disconnectFromRadio()
{
    if (m_state == State::Disconnected) {
        return;
    }
    // Politely close serial then tear each stream down.
    if (m_civ.ready) {
        sendSerialOpenClose(/*close=*/true);
    }
    for (StreamId id : {StreamId::Control, StreamId::Civ, StreamId::Audio}) {
        Stream& s = streamFor(id);
        if (s.gotRemoteSID) {
            sendDisconnect(id);
        }
        if (s.idleTimer) s.idleTimer->stop();
        if (s.pingTimer) s.pingTimer->stop();
    }
    if (m_reauthTimer) m_reauthTimer->stop();
    if (m_connectTimeout) m_connectTimeout->stop();

    const bool wasConnected = (m_state == State::Connected);
    setState(State::Disconnected);
    if (wasConnected) {
        emit disconnected();
    }
}

// ---------------------------------------------------------------------------
// Handshake (pkt3 -> pkt4 -> pkt6 -> pkt6), shared by all three streams.
// ---------------------------------------------------------------------------

QByteArray IcomUdpTransport::header(StreamId id, quint32 totalLen,
                                    quint16 type, quint16 seq)
{
    Stream& s = streamFor(id);
    QByteArray b(static_cast<int>(totalLen), '\0');
    putLe32(b, 0, totalLen);
    putLe16(b, 4, type);
    putLe16(b, 6, seq);
    putBe32(b, 8, s.localSID);
    putBe32(b, 12, s.remoteSID);
    return b;
}

void IcomUdpTransport::rawSend(StreamId id, const QByteArray& dg)
{
    Stream& s = streamFor(id);
    if (s.sock == nullptr) {
        return;
    }
    s.sock->writeDatagram(dg, m_params.address, s.port);
    if (shouldLog(dg)) {
        qCDebug(lcIcom).noquote() << QStringLiteral("Icom TX[%1] %2")
                                         .arg(streamName(static_cast<int>(id)),
                                              hexPreview(dg));
    }
}

void IcomUdpTransport::sendTracked(StreamId id, QByteArray dg)
{
    Stream& s = streamFor(id);
    putLe16(dg, 6, s.txSeq);          // stamp the tracked send-sequence
    s.txBuf.insert(s.txSeq, dg);
    s.txBuf.remove(static_cast<quint16>(s.txSeq - kTxBufDepth));
    rawSend(id, dg);
    ++s.txSeq;
}

void IcomUdpTransport::startStreamHandshake(StreamId id)
{
    qCInfo(lcIcom) << "Icom: opening" << streamName(static_cast<int>(id)) << "stream";
    sendAreYouThere(id);  // sent twice, like the reference, for loss tolerance
    sendAreYouThere(id);
}

void IcomUdpTransport::sendAreYouThere(StreamId id)
{
    rawSend(id, header(id, 0x10, 0x03, 0));
}

void IcomUdpTransport::sendAreYouReady(StreamId id)
{
    rawSend(id, header(id, 0x10, 0x06, 1));
}

void IcomUdpTransport::sendDisconnect(StreamId id)
{
    rawSend(id, header(id, 0x10, 0x05, 0));
    rawSend(id, header(id, 0x10, 0x05, 0));
}

void IcomUdpTransport::sendIdle(StreamId id)
{
    sendTracked(id, header(id, 0x10, 0x00, 0));  // type 0x00 keepalive
}

void IcomUdpTransport::sendPingRequest(StreamId id)
{
    Stream& s = streamFor(id);
    QByteArray p = header(id, 0x15, 0x07, s.pingSeq);
    p[16] = 0x00;  // reply flag: 0 = request
    // Our reply-id: rand, innerSeq lo/hi, 0x06 (matches the reference).
    p[17] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    p[18] = static_cast<char>(s.pingInnerSeq & 0xff);
    p[19] = static_cast<char>((s.pingInnerSeq >> 8) & 0xff);
    p[20] = 0x06;
    rawSend(id, p);
    ++s.pingSeq;
    ++s.pingInnerSeq;
}

void IcomUdpTransport::sendPingReply(StreamId id, const QByteArray& reqReplyId,
                                     quint16 seq)
{
    QByteArray p = header(id, 0x15, 0x07, seq);
    p[16] = 0x01;  // reply flag: 1 = answer
    for (int i = 0; i < 4 && i < reqReplyId.size(); ++i) {
        p[17 + i] = reqReplyId[i];
    }
    rawSend(id, p);
}

void IcomUdpTransport::handleRetransmit(StreamId id, const QByteArray& dg)
{
    Stream& s = streamFor(id);
    auto resend = [&](quint16 seq) {
        auto it = s.txBuf.constFind(seq);
        if (it != s.txBuf.constEnd()) {
            rawSend(id, it.value());
            rawSend(id, it.value());
        } else {
            // Not buffered — send an idle carrying that seq so the radio's
            // sequence view stays consistent.
            QByteArray idle = header(id, 0x10, 0x00, 0);
            putLe16(idle, 6, seq);
            rawSend(id, idle);
            rawSend(id, idle);
        }
    };

    if (dg.size() == 0x10) {  // single: wanted seq is in the header seq field
        resend(getLe16(dg, 6));
    } else if (dg.size() >= 0x18) {  // range list of (start,end) pairs from 0x10
        for (int off = 0x10; off + 4 <= dg.size(); off += 4) {
            quint16 start = getLe16(dg, off);
            quint16 end   = getLe16(dg, off + 2);
            for (quint32 seq = start; ; ++seq) {
                resend(static_cast<quint16>(seq));
                if (static_cast<quint16>(seq) == end) break;
            }
        }
    }
}

void IcomUdpTransport::onControlReadyRead()
{
    Stream& s = m_control;
    while (s.sock && s.sock->hasPendingDatagrams()) {
        QByteArray dg(static_cast<int>(s.sock->pendingDatagramSize()), '\0');
        QHostAddress from;
        s.sock->readDatagram(dg.data(), dg.size(), &from);
        if (!(from == m_params.address)) continue;
        handleControlDatagram(StreamId::Control, dg);
    }
}

void IcomUdpTransport::onCivReadyRead()
{
    Stream& s = m_civ;
    while (s.sock && s.sock->hasPendingDatagrams()) {
        QByteArray dg(static_cast<int>(s.sock->pendingDatagramSize()), '\0');
        QHostAddress from;
        s.sock->readDatagram(dg.data(), dg.size(), &from);
        if (!(from == m_params.address)) continue;
        handleControlDatagram(StreamId::Civ, dg);
    }
}

void IcomUdpTransport::onAudioReadyRead()
{
    Stream& s = m_audio;
    while (s.sock && s.sock->hasPendingDatagrams()) {
        QByteArray dg(static_cast<int>(s.sock->pendingDatagramSize()), '\0');
        QHostAddress from;
        s.sock->readDatagram(dg.data(), dg.size(), &from);
        if (!(from == m_params.address)) continue;
        handleControlDatagram(StreamId::Audio, dg);
    }
}

void IcomUdpTransport::handleControlDatagram(StreamId id, const QByteArray& dg)
{
    if (dg.size() < 16) {
        return;
    }
    if (shouldLog(dg)) {
        qCDebug(lcIcom).noquote() << QStringLiteral("Icom RX[%1] %2")
                                         .arg(streamName(static_cast<int>(id)),
                                              hexPreview(dg));
    }

    Stream& s = streamFor(id);

    // --- pkt7 ping (len 21, bytes[1..5] == 00 00 00 07 00; byte[0] varies) ---
    if (dg.size() == 21 && static_cast<quint8>(dg[1]) == 0x00
        && static_cast<quint8>(dg[2]) == 0x00 && static_cast<quint8>(dg[3]) == 0x00
        && static_cast<quint8>(dg[4]) == 0x07 && static_cast<quint8>(dg[5]) == 0x00) {
        if (static_cast<quint8>(dg[16]) == 0x00) {  // radio's request -> answer
            sendPingReply(id, dg.mid(17, 4), getLe16(dg, 6));
        }
        return;
    }

    const quint16 type = getLe16(dg, 4);

    // --- 16-byte control packets ---
    if (dg.size() == 0x10) {
        switch (type) {
        case 0x04:  // i-am-here: latch the radio's session id
            s.remoteSID = getBe32(dg, 8);
            s.gotRemoteSID = true;
            qCInfo(lcIcom).noquote()
                << QStringLiteral("Icom[%1]: i-am-here, remoteSID=%2")
                       .arg(streamName(static_cast<int>(id)))
                       .arg(s.remoteSID, 8, 16, QLatin1Char('0'));
            sendAreYouReady(id);
            sendAreYouReady(id);
            return;
        case 0x06:  // are-you-ready reply: base handshake complete
            if (!s.ready) {
                s.ready = true;
                onStreamReady(id);
            }
            return;
        case 0x00:  // idle keepalive from radio
            return;
        case 0x01:  // retransmit request (single)
            handleRetransmit(id, dg);
            return;
        case 0x05:  // radio tore the session down
            qCWarning(lcIcom) << "Icom[" << streamName(static_cast<int>(id))
                              << "]: radio sent disconnect";
            if (id == StreamId::Control) {
                disconnectFromRadio();
            }
            return;
        default:
            return;
        }
    }
    if (dg.size() == 0x18 && type == 0x01) {  // retransmit request (range)
        handleRetransmit(id, dg);
        return;
    }

    // --- Control-stream login / auth / conninfo replies (by length) ---
    if (id == StreamId::Control) {
        switch (dg.size()) {
        case 0x60:  // login response
            if (startsWith(dg, {0x60, 0x00, 0x00, 0x00, 0x00, 0x00})) {
                if (dg.size() >= 52
                    && static_cast<quint8>(dg[48]) == 0xff
                    && static_cast<quint8>(dg[49]) == 0xff
                    && static_cast<quint8>(dg[50]) == 0xff
                    && static_cast<quint8>(dg[51]) == 0xfe) {
                    fail(QStringLiteral("Invalid username or password."));
                    return;
                }
                m_loginResponseSeen = true;
                std::memcpy(m_authId, dg.constData() + 26, 6);
                m_gotAuthId = true;
                qCInfo(lcIcom) << "Icom: login accepted, authenticating";
                setState(State::Authenticating);
                // Ping + token-ack + idle keepalive + second auth.
                m_control.pingSeq = 2;
                m_control.pingTimer->start();
                sendAuth(0x02);
                m_control.idleTimer->start();
                sendAuth(0x05);
                m_reauthTimer->start();
            }
            return;
        case 0x40:  // auth reply
            if (startsWith(dg, {0x40, 0x00, 0x00, 0x00, 0x00, 0x00})) {
                if (static_cast<quint8>(dg[21]) == 0x05) {
                    m_authOk = true;
                    maybeRequestSerialAndAudio();
                }
            }
            return;
        case 0xA8:  // capabilities (a8) — carries the reply id we must echo
            if (startsWith(dg, {0xa8, 0x00, 0x00, 0x00, 0x00, 0x00})) {
                std::memcpy(m_a8ReplyId, dg.constData() + 66, 16);
                m_gotA8ReplyId = true;
                maybeRequestSerialAndAudio();
            }
            return;
        case 0x50:  // status
            if (startsWith(dg, {0x50, 0x00, 0x00, 0x00, 0x00, 0x00})) {
                if (dg.size() >= 51
                    && static_cast<quint8>(dg[48]) == 0xff
                    && static_cast<quint8>(dg[49]) == 0xff
                    && static_cast<quint8>(dg[50]) == 0xff) {
                    fail(QStringLiteral("Login rejected by the radio (try rebooting it)."));
                } else if (dg.size() >= 65
                           && static_cast<quint8>(dg[48]) == 0x00
                           && static_cast<quint8>(dg[49]) == 0x00
                           && static_cast<quint8>(dg[50]) == 0x00
                           && static_cast<quint8>(dg[64]) == 0x01) {
                    qCWarning(lcIcom) << "Icom: radio reported disconnected";
                    disconnectFromRadio();
                }
            }
            return;
        case 0x90:  // conninfo reply — success opens serial + audio
            if (!m_streamsRequested
                && startsWith(dg, {0x90, 0x00, 0x00, 0x00, 0x00, 0x00})
                && dg.size() >= 97 && static_cast<quint8>(dg[96]) == 0x01) {
                m_connectTimeout->stop();
                // The radio may reissue our session ids + auth id here.
                m_control.remoteSID = getBe32(dg, 8);
                m_control.localSID  = getBe32(dg, 12);
                std::memcpy(m_authId, dg.constData() + 26, 6);
                m_gotAuthId = true;
                m_streamsRequested = true;
                qCInfo(lcIcom) << "Icom: serial+audio request accepted; opening streams";
                openSerialAndAudioStreams();
            }
            return;
        default:
            return;
        }
    }

    // --- Serial (CI-V) data on the 50002 stream ---
    if (id == StreamId::Civ) {
        handleSerialDatagram(dg);
        return;
    }
    // --- Audio data on the 50003 stream ---
    if (id == StreamId::Audio) {
        handleAudioDatagram(dg);
        return;
    }
}

void IcomUdpTransport::onStreamReady(StreamId id)
{
    qCInfo(lcIcom) << "Icom:" << streamName(static_cast<int>(id)) << "handshake complete";
    switch (id) {
    case StreamId::Control:
        sendLogin();
        break;
    case StreamId::Civ:
        m_civ.pingSeq = 1;
        m_civ.pingTimer->start();
        m_civ.idleTimer->start();
        sendSerialOpenClose(/*close=*/false);
        // CI-V frames queued before the stream was ready can now flush.
        for (const QByteArray& f : m_pendingCiv) {
            sendCivFrame(f);
        }
        m_pendingCiv.clear();
        break;
    case StreamId::Audio:
        m_audio.pingSeq = 1;
        m_audio.pingTimer->start();
        break;
    }
}

// ---------------------------------------------------------------------------
// Control-stream login / auth / conninfo.
// ---------------------------------------------------------------------------

void IcomUdpTransport::sendLogin()
{
    QByteArray p = header(StreamId::Control, 0x80, 0x00, 0);  // seq stamped by sendTracked
    p[16] = 0x00; p[17] = 0x00; p[18] = 0x00; p[19] = 0x70;   // payloadsize 0x70
    p[20] = 0x01; p[21] = 0x00; p[22] = 0x00;
    p[23] = static_cast<char>(m_authInnerSeq & 0xff);
    p[24] = static_cast<char>((m_authInnerSeq >> 8) & 0xff);
    p[26] = static_cast<char>(QRandomGenerator::global()->bounded(256));  // authStartId
    p[27] = static_cast<char>(QRandomGenerator::global()->bounded(256));

    const QByteArray user = IcomSession::passcode(m_params.username);  // 16, obfuscated
    const QByteArray pass = IcomSession::passcode(m_params.password);  // 16, obfuscated
    std::memcpy(p.data() + 0x40, user.constData(), 16);
    std::memcpy(p.data() + 0x50, pass.constData(), 16);
    static const char kClientName[] = "icom-pc";  // computer name shown on radio
    std::memcpy(p.data() + 0x60, kClientName, sizeof(kClientName) - 1);

    qCInfo(lcIcom) << "Icom: sending login";
    sendTracked(StreamId::Control, p);
    ++m_authInnerSeq;
}

void IcomUdpTransport::sendAuth(quint8 magic)
{
    QByteArray p = header(StreamId::Control, 0x40, 0x00, 0);
    p[16] = 0x00; p[17] = 0x00; p[18] = 0x00; p[19] = 0x30;  // payloadsize 0x30
    p[20] = 0x01; p[21] = static_cast<char>(magic); p[22] = 0x00;
    p[23] = static_cast<char>(m_authInnerSeq & 0xff);
    p[24] = static_cast<char>((m_authInnerSeq >> 8) & 0xff);
    std::memcpy(p.data() + 26, m_authId, 6);
    sendTracked(StreamId::Control, p);
    ++m_authInnerSeq;
}

void IcomUdpTransport::maybeRequestSerialAndAudio()
{
    if (!m_streamsRequested && m_authOk && m_gotA8ReplyId) {
        sendRequestSerialAndAudio();
    }
}

void IcomUdpTransport::sendRequestSerialAndAudio()
{
    QByteArray p = header(StreamId::Control, 0x90, 0x00, 0);
    p[16] = 0x00; p[17] = 0x00; p[18] = 0x00; p[19] = 0x80;  // payloadsize 0x80
    p[20] = 0x01; p[21] = 0x03; p[22] = 0x00;
    p[23] = static_cast<char>(m_authInnerSeq & 0xff);
    p[24] = static_cast<char>((m_authInnerSeq >> 8) & 0xff);
    std::memcpy(p.data() + 26, m_authId, 6);         // 0x1a..0x1f
    std::memcpy(p.data() + 32, m_a8ReplyId, 16);     // 0x20..0x2f

    QByteArray dev = m_params.deviceName.toLatin1();
    if (dev.size() > 32) dev.truncate(32);
    std::memcpy(p.data() + 0x40, dev.constData(), dev.size());  // name[32]

    const QByteArray user = IcomSession::passcode(m_params.username);
    std::memcpy(p.data() + 0x60, user.constData(), 16);         // obfuscated username

    p[0x70] = 0x01;  // rxenable
    p[0x71] = 0x01;  // txenable
    p[0x72] = 0x04;  // rxcodec: 48kHz S16LE mono
    p[0x73] = 0x04;  // txcodec
    const quint32 sr = kAudioSampleRate;             // 48000
    putBe32(p, 0x74, sr);                            // rxsample
    putBe32(p, 0x78, sr);                            // txsample
    putBe32(p, 0x7c, kCivPort);                      // civport 50002
    putBe32(p, 0x80, kAudioPort);                    // audioport 50003
    const quint16 txBufMs = 300;                     // txSeqBufLength
    p[0x86] = static_cast<char>((txBufMs >> 8) & 0xff);
    p[0x87] = static_cast<char>(txBufMs & 0xff);
    p[0x88] = 0x01;                                  // convert

    qCInfo(lcIcom) << "Icom: requesting serial + audio streams";
    sendTracked(StreamId::Control, p);
    ++m_authInnerSeq;
}

void IcomUdpTransport::openSerialAndAudioStreams()
{
    // We consider the session live once the radio accepts us; CI-V + audio
    // begin flowing as their sub-streams complete their own handshakes.
    setState(State::Connected);
    emit connected();

    if (m_civ.sock->state() != QAbstractSocket::BoundState) {
        m_civ.sock->bind(QHostAddress::AnyIPv4, 0);
    }
    if (m_audio.sock->state() != QAbstractSocket::BoundState) {
        m_audio.sock->bind(QHostAddress::AnyIPv4, 0);
    }
    startStreamHandshake(StreamId::Civ);
    startStreamHandshake(StreamId::Audio);
}

// ---------------------------------------------------------------------------
// Serial (CI-V) framing on 50002.
// ---------------------------------------------------------------------------

void IcomUdpTransport::sendSerialOpenClose(bool close)
{
    QByteArray p = header(StreamId::Civ, 0x16, 0x00, 0);
    p[16] = static_cast<char>(0xc0);
    p[17] = 0x01;
    p[18] = 0x00;
    p[19] = static_cast<char>((m_civ.streamSeq >> 8) & 0xff);
    p[20] = static_cast<char>(m_civ.streamSeq & 0xff);
    p[21] = close ? 0x00 : 0x05;
    sendTracked(StreamId::Civ, p);
    ++m_civ.streamSeq;
}

void IcomUdpTransport::sendCivFrame(const QByteArray& civFrame)
{
    if (!m_civ.ready) {
        // Queue until the serial stream is up (IcomBackend may push CI-V as
        // soon as connected() fires).
        m_pendingCiv.append(civFrame);
        return;
    }
    const int l = civFrame.size();
    QByteArray p = header(StreamId::Civ, static_cast<quint32>(0x15 + l), 0x00, 0);
    p[16] = static_cast<char>(0xc1);
    p[17] = static_cast<char>(l & 0xff);
    p[18] = 0x00;
    p[19] = static_cast<char>((m_civ.streamSeq >> 8) & 0xff);
    p[20] = static_cast<char>(m_civ.streamSeq & 0xff);
    std::memcpy(p.data() + 21, civFrame.constData(), l);
    sendTracked(StreamId::Civ, p);
    ++m_civ.streamSeq;
}

void IcomUdpTransport::handleSerialDatagram(const QByteArray& dg)
{
    // A serial data frame: byte[16]==0xc1 and total len (byte0 - 0x15) matches
    // the inner length (byte17).  The CI-V frame itself starts at offset 21.
    if (dg.size() >= 22
        && static_cast<quint8>(dg[16]) == 0xc1
        && static_cast<quint8>(static_cast<quint8>(dg[0]) - 0x15) == static_cast<quint8>(dg[17])) {
        const QByteArray civ = dg.mid(21);
        if (!civ.isEmpty()) {
            emit civFrameReceived(civ);
        }
    }
}

// ---------------------------------------------------------------------------
// Audio on 50003 (RX-first: 48 kHz S16LE mono).
// ---------------------------------------------------------------------------

void IcomUdpTransport::handleAudioDatagram(const QByteArray& dg)
{
    if (dg.size() >= 580
        && (startsWith(dg, {0x6c, 0x05, 0x00, 0x00, 0x00, 0x00})
            || startsWith(dg, {0x44, 0x02, 0x00, 0x00, 0x00, 0x00}))) {
        emit audioPayloadReceived(dg.mid(24));  // raw S16LE PCM
    }
}

void IcomUdpTransport::sendAudioPayload(const QByteArray& payload)
{
    // TX audio is a deliberate RX-first stub (#9). Left unimplemented on the
    // wire; log once so a stray call is visible rather than silently dropped.
    Q_UNUSED(payload);
    qCDebug(lcIcom) << "Icom: sendAudioPayload ignored (TX not yet implemented)";
}

// ---------------------------------------------------------------------------
// State + teardown.
// ---------------------------------------------------------------------------

void IcomUdpTransport::setState(State s)
{
    if (m_state == s) {
        return;
    }
    m_state = s;
    emit stateChanged(s);
}

void IcomUdpTransport::fail(const QString& why)
{
    qCWarning(lcIcom).noquote() << "Icom: connection failed —" << why;
    for (StreamId id : {StreamId::Control, StreamId::Civ, StreamId::Audio}) {
        Stream& st = streamFor(id);
        if (st.idleTimer) st.idleTimer->stop();
        if (st.pingTimer) st.pingTimer->stop();
    }
    if (m_reauthTimer) m_reauthTimer->stop();
    if (m_connectTimeout) m_connectTimeout->stop();
    setState(State::Error);
    emit errorOccurred(why);
}

void IcomUdpTransport::teardown()
{
    for (Stream* s : {&m_control, &m_civ, &m_audio}) {
        if (s->idleTimer) s->idleTimer->stop();
        if (s->pingTimer) s->pingTimer->stop();
    }
    if (m_reauthTimer) m_reauthTimer->stop();
    if (m_connectTimeout) m_connectTimeout->stop();
    m_state = State::Disconnected;
}

} // namespace AetherSDR
