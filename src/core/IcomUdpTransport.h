#pragma once

#include <QByteArray>
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
// Icom's IP-remote stack uses THREE UDP ports, each a semi-independent stream
// sharing a common session/framing layer:
//   * 50001 Control  — session establishment, are-you-there / are-you-ready,
//                       periodic idle keepalive, sequence + retransmit, and
//                       token-based login (username/password).
//   * 50002 CI-V     — carries standard CI-V frames (parsed by IcomCiv codec).
//   * 50003 Audio    — negotiated mu-law / PCM RX (and later TX) audio.
//
// CLEAN-ROOM POSTURE (Constitution Principle IV — read-and-reimplement):
//   The port numbers, the fact that transport is UDP, and that 50002 carries
//   CI-V are PUBLIC (Icom manuals / CI-V reference).  The session-wrapper byte
//   layout (packet opcodes, sequence/retransmit fields, login token exchange)
//   is derived from clean sources: first-party packet captures AND reading
//   wfview's open-source code (a permitted open-source reference under
//   Principle IV) — then implemented here as AetherSDR's OWN code.  Do NOT copy
//   wfview verbatim, and never touch the proprietary RS-BA1 binary.  See
//   docs/icom-cleanroom-design.md.  Every seam below is marked
//   `TODO(cleanroom #10)`; the class ships the framework (sockets, state
//   machine, keepalive, teardown) with those encoders/decoders as the work left.
//
// Designed to live on a worker thread like RadioConnection (#502): construct,
// moveToThread(), call init(), then drive via queued slot calls.
class IcomUdpTransport : public QObject {
    Q_OBJECT

public:
    enum class State {
        Disconnected,
        Connecting,      // control-port handshake in progress
        Authenticating,  // login token exchange
        Connected,       // control session live; CI-V + audio may open
        Error
    };
    Q_ENUM(State)

    // Icom's fixed default UDP ports.  Per Icom docs these are part of the
    // protocol and must not be remapped.
    static constexpr quint16 kControlPort = 50001;
    static constexpr quint16 kCivPort     = 50002;
    static constexpr quint16 kAudioPort   = 50003;

    // Idle keepalive cadence.  PROVISIONAL — confirm against capture
    // (TODO(cleanroom #10)); losing keepalive makes the radio drop the session.
    static constexpr int kKeepaliveIntervalMs = 100;
    static constexpr int kAreYouThereMs       = 500;

    struct ConnectParams {
        QHostAddress address;
        quint16 controlPort{kControlPort};
        QString username;
        QString password;
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
    // radio.  Wraps it in the 50002 session envelope and transmits.
    void sendCivFrame(const QByteArray& civFrame);

    // Send an encoded audio payload (mu-law/PCM) on the 50003 stream (TX path).
    void sendAudioPayload(const QByteArray& payload);

signals:
    void stateChanged(State state);
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    // A CI-V frame (FE FE .. FD) recovered from the 50002 session envelope,
    // ready for the IcomCiv codec.
    void civFrameReceived(const QByteArray& civFrame);

    // A raw audio payload recovered from the 50003 session envelope, ready for
    // the IcomAudio codec (still in the radio's negotiated format).
    void audioPayloadReceived(const QByteArray& payload);

    // Sequence-gap / retransmit telemetry for the diagnostics panel, mirroring
    // KiwiSdrClient's sequence-gap counters.
    void sequenceGap(quint16 port, quint64 totalGaps);

private slots:
    void onControlReadyRead();
    void onCivReadyRead();
    void onAudioReadyRead();
    void onKeepaliveTick();

private:
    void setState(State s);
    void teardownSockets();

    // --- Proprietary session codec seams (fill in from capture) ------------
    // These four are the ONLY places the reverse-engineered wire format lives.
    // TODO(cleanroom #10): implement from docs/icom-cleanroom-design.md, NOT
    // from wfview.  Until then they are documented no-ops so the class links
    // and the socket/state-machine scaffolding can be reviewed independently.
    QByteArray buildControlPacket(quint8 opcode, const QByteArray& body);
    void handleControlDatagram(const QByteArray& datagram);
    QByteArray wrapStreamPayload(quint16 port, const QByteArray& payload);
    QByteArray unwrapStreamPayload(quint16 port, const QByteArray& datagram,
                                   bool* ok);

    State m_state{State::Disconnected};
    ConnectParams m_params;

    QUdpSocket* m_controlSocket{nullptr};
    QUdpSocket* m_civSocket{nullptr};
    QUdpSocket* m_audioSocket{nullptr};
    QTimer* m_keepalive{nullptr};

    // Session identifiers negotiated during the control handshake.
    // TODO(cleanroom #10): populate from the real handshake.
    quint32 m_localId{0};
    quint32 m_remoteId{0};
    quint16 m_txSeq{0};

    quint64 m_civSeqGaps{0};
    quint64 m_audioSeqGaps{0};
};

} // namespace AetherSDR
