#include "IcomDiscovery.h"

#include "IcomSessionCodec.h"
#include "LogManager.h"

#include <QElapsedTimer>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QTimer>
#include <QUdpSocket>

namespace AetherSDR {

namespace {
// One process-wide monotonic clock for last-seen bookkeeping.
QElapsedTimer& monoClock()
{
    static QElapsedTimer t;
    if (!t.isValid()) {
        t.start();
    }
    return t;
}
}  // namespace

QVector<quint32> icomSweepHosts(quint32 ipv4, quint32 netmask, int cap)
{
    QVector<quint32> hosts;
    if (netmask == 0 || netmask == 0xFFFFFFFFu) {
        return hosts;  // 0.0.0.0 mask or /32 — nothing sensible to sweep
    }
    const quint32 network = ipv4 & netmask;
    const quint32 broadcast = network | ~netmask;
    if (broadcast <= network + 1) {
        return hosts;  // /31 or degenerate: no usable host range
    }
    const quint32 count = broadcast - network - 1;  // usable hosts
    if (count > static_cast<quint32>(cap)) {
        return hosts;  // too large (e.g. /16) — skip rather than flood
    }
    hosts.reserve(static_cast<int>(count));
    for (quint32 addr = network + 1; addr < broadcast; ++addr) {
        hosts.append(addr);
    }
    return hosts;
}

IcomDiscovery::IcomDiscovery(QObject* parent)
    : QObject(parent)
{
}

IcomDiscovery::~IcomDiscovery()
{
    stopSweeping();
}

QList<QHostAddress> IcomDiscovery::discovered() const
{
    QList<QHostAddress> out;
    for (auto it = m_lastSeen.constBegin(); it != m_lastSeen.constEnd(); ++it) {
        out.append(QHostAddress(it.key()));
    }
    return out;
}

void IcomDiscovery::startSweeping()
{
    if (m_running) {
        return;
    }
    if (m_socket == nullptr) {
        m_socket = new QUdpSocket(this);
        connect(m_socket, &QUdpSocket::readyRead,
                this, &IcomDiscovery::onReadyRead);
    }
    if (!m_socket->bind(QHostAddress::AnyIPv4, 0)) {
        qCWarning(lcIcom) << "IcomDiscovery: UDP bind failed —"
                          << m_socket->errorString();
        return;
    }
    if (m_sweepTimer == nullptr) {
        m_sweepTimer = new QTimer(this);
        m_sweepTimer->setInterval(kSweepIntervalMs);
        connect(m_sweepTimer, &QTimer::timeout, this, &IcomDiscovery::onSweepTick);
    }
    if (m_staleTimer == nullptr) {
        m_staleTimer = new QTimer(this);
        m_staleTimer->setInterval(kStaleCheckMs);
        connect(m_staleTimer, &QTimer::timeout, this, &IcomDiscovery::onStaleTick);
    }
    m_localId = QRandomGenerator::global()->generate();
    m_running = true;
    m_sweepTimer->start();
    m_staleTimer->start();
    sweepNow();
}

void IcomDiscovery::stopSweeping()
{
    m_running = false;
    if (m_sweepTimer != nullptr) {
        m_sweepTimer->stop();
    }
    if (m_staleTimer != nullptr) {
        m_staleTimer->stop();
    }
    if (m_socket != nullptr) {
        m_socket->close();
    }
    m_lastSeen.clear();
}

void IcomDiscovery::onSweepTick()
{
    sweepNow();
}

void IcomDiscovery::sweepNow()
{
    if (m_socket == nullptr || !m_running) {
        return;
    }
    const QList<QHostAddress> targets = sweepTargets();
    for (const QHostAddress& target : targets) {
        const QByteArray probe =
            IcomSession::encodeAreYouThere(m_seq++, m_localId);
        m_socket->writeDatagram(probe, target, kControlPort);
    }
    qCDebug(lcIcom) << "IcomDiscovery: swept" << targets.size() << "hosts";
}

QList<QHostAddress> IcomDiscovery::sweepTargets() const
{
    QList<QHostAddress> targets;
    QSet<quint32> ownAddresses;

    // Collect our own IPv4 addresses first so we don't probe ourselves.
    const QList<QNetworkInterface> ifaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : ifaces) {
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            if (ip.protocol() == QAbstractSocket::IPv4Protocol) {
                ownAddresses.insert(ip.toIPv4Address());
            }
        }
    }

    for (const QNetworkInterface& iface : ifaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress ip = entry.ip();
            const QHostAddress mask = entry.netmask();
            if (ip.protocol() != QAbstractSocket::IPv4Protocol
                || mask.protocol() != QAbstractSocket::IPv4Protocol) {
                continue;
            }
            const QVector<quint32> hosts = icomSweepHosts(
                ip.toIPv4Address(), mask.toIPv4Address(), kMaxHostsPerSubnet);
            for (quint32 addr : hosts) {
                if (!ownAddresses.contains(addr)) {
                    targets.append(QHostAddress(addr));
                }
            }
        }
    }
    return targets;
}

void IcomDiscovery::onReadyRead()
{
    while (m_socket != nullptr && m_socket->hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket->receiveDatagram();
        const std::optional<IcomSession::ParsedControl> parsed =
            IcomSession::parseControl(dg.data());
        if (!parsed.has_value()
            || parsed->kind != IcomSession::ControlKind::IAmHere) {
            continue;  // not an Icom handshake reply
        }
        const QHostAddress sender = dg.senderAddress();
        if (sender.isNull()) {
            continue;
        }
        const QString key = sender.toString();
        const bool isNew = !m_lastSeen.contains(key);
        m_lastSeen.insert(key, monoClock().elapsed());

        // Politeness: free the radio's single-session slot immediately by
        // tearing down the half-open session we just opened.  The radio's
        // session id is its `sentid` in the I-am-here header.
        const QByteArray bye = IcomSession::encodeDisconnect(
            m_seq++, m_localId, parsed->header.sentId);
        m_socket->writeDatagram(bye, sender, kControlPort);

        if (isNew) {
            qCInfo(lcIcom).noquote()
                << QStringLiteral("IcomDiscovery: Icom radio detected at %1").arg(key);
            emit icomRadioFound(sender);
        }
    }
}

void IcomDiscovery::onStaleTick()
{
    const qint64 now = monoClock().elapsed();
    auto it = m_lastSeen.begin();
    while (it != m_lastSeen.end()) {
        if (now - it.value() > kStaleTimeoutMs) {
            const QHostAddress lost(it.key());
            it = m_lastSeen.erase(it);
            emit icomRadioLost(lost);
        } else {
            ++it;
        }
    }
}

} // namespace AetherSDR
