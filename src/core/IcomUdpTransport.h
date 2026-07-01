#pragma once

#include <QByteArray>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <cstdint>

class QUdpSocket;
class QTimer;

namespace AetherSDR {

// Session/transport layer for network-capable Icom radios (IC-705 over Wi-Fi;
// IC-9700 / IC-7610 / IC-7300MK2 over Ethernet).  Issue #3 / epic #1.
//
// Icom's IP-remote stack uses THREE UDP ports, each an INDEPENDENT session with
// its own handshake, session ids and sequence counters, sharing only the login
// the control stream performs:
//   * 50001 Control  — handshake, login (obfuscated username/password), token
//                      auth, conninfo, keepalive, ping, retransmit.
//   * 50002 CI-V     — standard CI-V frames wrapped in a 0xC1 stream envelope.
//   * 50003 Audio    — RX (and later TX) audio, 48 kHz S16LE mono for IC-705.
//
// PROVENANCE (Constitution Principle IV — read-and-reimplement): the packet
// layouts, login/token flow and stream framing were derived from the DOCUMENTED
// open-source references (kappanhang's Go implementation with verbatim real-
// radio captures, and wfview's packettypes.h), then re-expressed here as
// AetherSDR's own code.  The proprietary RS-BA1 binary was never consulted.
// The username/password obfuscation table is hardware-confirmed (see
// IcomSession::passcode); the rest is best-effort against the references and is
// verified end-to-end against a real radio (build-and-iterate).
//
// Threading: lives on a worker thread like RadioConnection (#502): construct,
// moveToThread(), then drive via queued slot calls.
class IcomUdpTransport : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,      // control-port handshake / login in progress
        Authenticating,  // token exchange
        Connected,       // control + serial (+ audio) live
        Error
    };
    Q_ENUM(State)

    // Icom's fixed default UDP ports.
    static constexpr quint16 kControlPort = 50001;
    static constexpr quint16 kCivPort     = 50002;
    static constexpr quint16 kAudioPort   = 50003;

    // Timing (milliseconds), matching the references.
    static constexpr int kIdleIntervalMs   = 100;   // pkt0 keepalive
    static constexpr int kPingIntervalMs    = 3000;  // pkt7 ping
    static constexpr int kReauthIntervalMs  = 60000; // token renewal
    static constexpr int kConnectTimeoutMs  = 8000;  // give up if no login

    // RX audio format the IC-705 delivers for the codec we request (0x04).
    static constexpr int kAudioSampleRate = 48000;   // S16LE, mono

    struct ConnectParams {
        QHostAddress address;
        quint16 controlPort{kControlPort};
        QString username;
        QString password;
        // Plain-text radio/device name sent in the serial+audio request.  The
        // radio echoes it back and it labels our session; defaults to IC-705.
        QString deviceName{QStringLiteral("IC-705")};
    };

    explicit IcomUdpTransport(QObject* parent = nullptr);
    ~IcomUdpTransport() override;

    State state() const { return m_state; }
    bool isConnected() const { return m_state == State::Connected; }

public slots:
    void init();  // create sockets + timers on the worker thread
    void connectToRadio(const ConnectParams& params);
    void disconnectFromRadio();

    // Send a fully-framed CI-V frame (FE FE .. FD, built by IcomCiv) to the
    // radio over the 50002 serial stream.
    void sendCivFrame(const QByteArray& civFrame);

    // Send RX/TX audio payload on the 50003 stream (TX path — RX-first stub).
    void sendAudioPayload(const QByteArray& payload);

signals:
    void stateChanged(State state);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    // A CI-V frame (FE FE .. FD) recovered from the 50002 stream envelope.
    void civFrameReceived(const QByteArray& civFrame);

    // A raw RX audio payload (48 kHz S16LE mono for IC-705) from 50003.
    void audioPayloadReceived(const QByteArray& payload);

    // Sequence-gap telemetry for diagnostics.
    void sequenceGap(quint16 port, quint64 totalGaps);

private slots:
    void onControlReadyRead();
    void onCivReadyRead();
    void onAudioReadyRead();

private:
    // Which of the three UDP streams a datagram / action belongs to.
    enum class StreamId { Control, Civ, Audio };

    // Per-stream session state.  Each stream has its own session ids, tracked
    // send-sequence, keepalive/ping timers, and a small retransmit buffer.
    struct Stream {
        QUdpSocket* sock{nullptr};
        quint16     port{0};
        quint32     localSID{0};
        quint32     remoteSID{0};
        bool        gotRemoteSID{false};
        bool        ready{false};        // pkt3/4/6 handshake complete

        quint16     txSeq{1};            // pkt0 tracked send-sequence
        quint16     pingSeq{0};          // pkt7 send-sequence
        quint16     pingInnerSeq{0x8304};
        quint16     streamSeq{0};        // serial/audio inner sequence

        QTimer*     idleTimer{nullptr};  // pkt0 keepalive
        QTimer*     pingTimer{nullptr};  // pkt7 ping

        QHash<quint16, QByteArray> txBuf; // seq -> last sent tracked packet
        int         lastRxSeq{-1};        // for gap detection
        quint64     seqGaps{0};
    };

    Stream& streamFor(StreamId id);
    Stream* streamForSocket(QObject* sock);

    // --- Handshake (shared by all three streams) ------------------------
    void startStreamHandshake(StreamId id);
    void onStreamReady(StreamId id);            // pkt3/4/6 done
    void handleControlDatagram(StreamId id, const QByteArray& dg);

    // --- Low-level send helpers -----------------------------------------
    void rawSend(StreamId id, const QByteArray& dg);           // untracked
    void sendTracked(StreamId id, QByteArray dg);              // stamps txSeq
    void sendAreYouThere(StreamId id);
    void sendAreYouReady(StreamId id);
    void sendDisconnect(StreamId id);
    void sendIdle(StreamId id);
    void sendPingRequest(StreamId id);
    void sendPingReply(StreamId id, const QByteArray& reqReplyId, quint16 seq);
    void handleRetransmit(StreamId id, const QByteArray& dg);

    // --- Control-stream login / auth / conninfo -------------------------
    void sendLogin();
    void sendAuth(quint8 magic);
    void sendRequestSerialAndAudio();
    void maybeRequestSerialAndAudio();
    void openSerialAndAudioStreams();

    // --- Serial (CI-V) + audio framing ----------------------------------
    void sendSerialOpenClose(bool close);
    void handleSerialDatagram(const QByteArray& dg);
    void handleAudioDatagram(const QByteArray& dg);

    void setState(State s);
    void teardown();
    void fail(const QString& why);

    // Build the shared 16-byte header (len/type/seq little-endian, session ids
    // big-endian) for a stream, with a body appended.  seq is left at 0 for
    // tracked packets (stamped later) or set for handshake packets.
    QByteArray header(StreamId id, quint32 totalLen, quint16 type, quint16 seq);

    State m_state{State::Disconnected};
    ConnectParams m_params;

    Stream m_control;
    Stream m_civ;
    Stream m_audio;

    // Control-stream auth bookkeeping.
    quint16 m_authInnerSeq{0};
    quint8  m_authId[6]{};
    bool    m_gotAuthId{false};
    quint8  m_a8ReplyId[16]{};
    bool    m_gotA8ReplyId{false};
    bool    m_authOk{false};
    bool    m_streamsRequested{false};
    bool    m_loginResponseSeen{false};

    QTimer* m_reauthTimer{nullptr};
    QTimer* m_connectTimeout{nullptr};

    // CI-V frames pushed before the serial stream finished its handshake.
    QList<QByteArray> m_pendingCiv;
};

} // namespace AetherSDR
