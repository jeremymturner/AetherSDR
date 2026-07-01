// Live connect harness (NOT a ctest — needs a real radio on the LAN).
//
//   ./icom_live_connect <ip> <username> <password> [modelKey] [seconds]
//
// Drives the real IcomUdpTransport against a physical radio, printing the
// control-plane packet trace (aether.icom debug) plus decoded CI-V frequency /
// mode replies.  Used to verify the protocol end-to-end and iterate.
#include "core/IcomCivCodec.h"
#include "core/IcomUdpTransport.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QTimer>

#include <cstdio>

using namespace AetherSDR;

static QString hx(const QByteArray& b)
{
    QString s;
    for (char c : b) s += QString::asprintf("%02x ", static_cast<quint8>(c));
    return s.trimmed();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    // Set AETHER_ICOM_QUIET=1 to silence the per-packet hex trace (useful for
    // measuring audio throughput without logging overhead).
    if (qEnvironmentVariableIsEmpty("AETHER_ICOM_QUIET")) {
        QLoggingCategory::setFilterRules(QStringLiteral("aether.icom.debug=true"));
    } else {
        QLoggingCategory::setFilterRules(QStringLiteral("aether.icom.debug=false"));
    }

    const QStringList args = app.arguments();
    if (args.size() < 4) {
        std::fprintf(stderr, "usage: %s <ip> <user> <pass> [model] [seconds]\n",
                     argv[0]);
        return 2;
    }
    const QString ip    = args.at(1);
    const QString user  = args.at(2);
    const QString pass  = args.at(3);
    const QString model = args.size() > 4 ? args.at(4) : QStringLiteral("IC-705");
    const int seconds   = args.size() > 5 ? args.at(5).toInt() : 20;

    // IC-705 default CI-V address (matches IcomRadioCapabilities).
    quint8 civAddr = 0xA4;
    if (model.contains(QStringLiteral("9700"))) civAddr = 0xA2;
    else if (model.contains(QStringLiteral("7610"))) civAddr = 0x98;
    else if (model.contains(QStringLiteral("7300"))) civAddr = 0x94;

    IcomUdpTransport transport;
    transport.init();

    static long audioPackets = 0;
    static long audioBytes = 0;
    QObject::connect(&transport, &IcomUdpTransport::audioPayloadReceived,
                     [&](const QByteArray& pcm) {
        ++audioPackets;
        audioBytes += pcm.size();
        if (audioPackets <= 3 || audioPackets % 100 == 0) {
            std::printf("AUDIO RX #%ld: %d bytes (%ld total)\n",
                        audioPackets, pcm.size(), audioBytes);
            std::fflush(stdout);
        }
    });

    QObject::connect(&transport, &IcomUdpTransport::connected, [&] {
        std::printf("\n=== CONNECTED ===\n");
        std::fflush(stdout);
        // Give the serial stream a moment to finish its handshake, then poll.
        QTimer::singleShot(800, [&] {
            std::printf(">>> readFrequency to 0x%02x AND broadcast 0x00\n", civAddr);
            std::fflush(stdout);
            transport.sendCivFrame(IcomCiv::readFrequency(civAddr));
            transport.sendCivFrame(IcomCiv::readMode(civAddr));
            // Broadcast: the radio answers from its real CI-V address even if
            // the configured address differs from our guess.
            transport.sendCivFrame(IcomCiv::readFrequency(0x00));
            transport.sendCivFrame(IcomCiv::readMode(0x00));
        });
    });
    QObject::connect(&transport, &IcomUdpTransport::errorOccurred,
                     [&](const QString& e) {
        std::printf("\n=== ERROR: %s ===\n", e.toUtf8().constData());
        std::fflush(stdout);
        QTimer::singleShot(200, &app, [&] { app.exit(1); });
    });
    QObject::connect(&transport, &IcomUdpTransport::disconnected, [&] {
        std::printf("\n=== DISCONNECTED ===\n");
        std::fflush(stdout);
    });
    QObject::connect(&transport, &IcomUdpTransport::civFrameReceived,
                     [&](const QByteArray& civ) {
        std::printf("CI-V RX: %s\n", hx(civ).toUtf8().constData());
        if (const auto frame = IcomCiv::parseFrame(civ)) {
            std::printf("   -> to=0x%02x from=0x%02x\n",
                        static_cast<int>(frame->toAddr),
                        static_cast<int>(frame->fromAddr));
            if (const auto f = IcomCiv::parseFrequencyResponse(*frame)) {
                std::printf("   -> frequency = %llu Hz\n",
                            static_cast<unsigned long long>(*f));
            }
            if (const auto m = IcomCiv::parseModeResponse(*frame)) {
                std::printf("   -> mode code = 0x%02x\n",
                            static_cast<int>(m->mode));
            }
        }
        std::fflush(stdout);
    });

    IcomUdpTransport::ConnectParams p;
    p.address  = QHostAddress(ip);
    p.username = user;
    p.password = pass;
    p.deviceName = model;
    std::printf("connecting to %s as '%s' (model %s)...\n",
                ip.toUtf8().constData(), user.toUtf8().constData(),
                model.toUtf8().constData());
    std::fflush(stdout);
    transport.connectToRadio(p);

    QTimer::singleShot(seconds * 1000, &app, [&] {
        std::printf("\n=== timeout after %d s, exiting ===\n", seconds);
        std::printf("=== audio: %ld packets, %ld bytes (~%ld ms @48k S16LE mono) ===\n",
                    audioPackets, audioBytes, audioBytes / 2 / 48);
        transport.disconnectFromRadio();
        QTimer::singleShot(300, &app, [&] { app.quit(); });
    });
    return app.exec();
}
