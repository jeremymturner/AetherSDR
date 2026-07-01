#pragma once

#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVector>

#include <cstdint>

class QUdpSocket;
class QTimer;

namespace AetherSDR {

// Enumerate the host addresses of an IPv4 subnet (network+1 .. broadcast-1),
// capped at `cap` hosts.  Returns empty when the subnet is a single host or
// larger than the cap (so we never sweep a /16).  Pure + freestanding-testable.
QVector<quint32> icomSweepHosts(quint32 ipv4, quint32 netmask, int cap);

// Active LAN discovery for network-capable Icom radios (#5).  Icom's built-in
// interface does not broadcast/advertise, so we actively probe: send the Icom
// `are-you-there` control packet to every host on each local IPv4 subnet at
// UDP 50001 and listen for the `i-am-here` reply.  A reply fingerprints the
// host as an Icom IP-remote radio WITHOUT authentication (login is a later leg);
// the model is NOT knowable pre-auth, so we only surface the address.
//
// Politeness: on each `i-am-here` we immediately send a clean `disconnect`, so
// the probe never holds the radio's single network-session slot, and we never
// proceed into the ready/login legs — so a live RS-BA1/wfview session can't be
// disturbed.
class IcomDiscovery : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 kControlPort     = 50001;   // == IcomUdpTransport::kControlPort
    static constexpr int kSweepIntervalMs     = 15000;   // re-sweep cadence
    static constexpr int kStaleTimeoutMs      = 48000;   // drop after ~3 missed sweeps
    static constexpr int kStaleCheckMs        = 5000;
    static constexpr int kMaxHostsPerSubnet   = 1024;    // never sweep bigger than /22

    explicit IcomDiscovery(QObject* parent = nullptr);
    ~IcomDiscovery() override;

    QList<QHostAddress> discovered() const;

public slots:
    void startSweeping();  // bind, sweep now, arm periodic re-sweep + stale sweep
    void stopSweeping();

signals:
    void icomRadioFound(const QHostAddress& address);
    void icomRadioLost(const QHostAddress& address);

private slots:
    void onReadyRead();
    void onSweepTick();
    void onStaleTick();

private:
    void sweepNow();
    QList<QHostAddress> sweepTargets() const;

    QUdpSocket* m_socket{nullptr};
    QTimer*     m_sweepTimer{nullptr};
    QTimer*     m_staleTimer{nullptr};
    quint32     m_localId{0};
    quint16     m_seq{0};
    bool        m_running{false};

    // address string -> last-seen monotonic ms; presence tracked for lost().
    QMap<QString, qint64> m_lastSeen;
};

} // namespace AetherSDR
