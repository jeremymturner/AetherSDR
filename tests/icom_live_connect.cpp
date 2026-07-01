// Live connect harness (NOT a ctest — needs a real radio on the LAN).
//
//   ./icom_live_connect <ip> <username> <password> [modelKey] [seconds]
//
// Drives the real IcomUdpTransport against a physical radio, printing the
// control-plane packet trace (aether.icom debug) plus decoded CI-V frequency /
// mode replies.  Used to verify the protocol end-to-end and iterate.
#include "core/IcomCivCodec.h"
#include "core/IcomScopeParser.h"
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
            // Enable the spectrum scope + waveform data output over CI-V.
            // Try the main/sub-selector form of scope-ON (27 10 00 01) which
            // several Icom radios require even with a single scope.
            std::printf(">>> enabling scope: 27 10 00 01 (main, ON) + 27 11 01 (data out)\n");
            std::fflush(stdout);
            transport.sendCivFrame(IcomCiv::encodeFrame(
                civAddr, 0xE0, 0x27, QByteArray::fromHex("100001")));  // scope main ON
            transport.sendCivFrame(IcomCiv::encodeFrame(
                civAddr, 0xE0, 0x27, QByteArray::fromHex("1101")));    // data output ON
            // Read back scope on/off + data-output state.
            transport.sendCivFrame(IcomCiv::encodeFrame(
                civAddr, 0xE0, 0x27, QByteArray::fromHex("10")));
            transport.sendCivFrame(IcomCiv::encodeFrame(
                civAddr, 0xE0, 0x27, QByteArray::fromHex("11")));
        });
        // Tune to a guaranteed-strong FM broadcast signal to prove the scope
        // produces real (non-zero) waveform data end-to-end.
        QTimer::singleShot(2500, [&] {
            std::printf(">>> tuning to 101.100 MHz WFM to look for signal\n");
            std::fflush(stdout);
            transport.sendCivFrame(IcomCiv::setMode(civAddr, IcomCiv::CivMode::Wfm));
            transport.sendCivFrame(IcomCiv::setFrequency(civAddr, 101100000ULL));
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
    static long scopeFrames = 0;
    QObject::connect(&transport, &IcomUdpTransport::civFrameReceived,
                     [&](const QByteArray& civ) {
        // Scope waveform frames (cmd 0x27) can be large + frequent; dump the
        // first few in full, then just count.
        if (civ.size() >= 6 && static_cast<quint8>(civ[4]) == 0x27) {
            // Short 0x27 frames are readback/ack replies (scope on/off, data
            // output state) — print them verbatim.
            if (civ.size() < 30) {
                std::printf("SCOPE-CTL: %s\n", hx(civ).toUtf8().constData());
                std::fflush(stdout);
                return;
            }
            ++scopeFrames;
            // Waveform starts at index 21 (after seq/seqMax/mode/2×5-byte freq/
            // oor) and runs to just before the trailing FD.
            static int maxAmp = 0;
            static int maxLen = 0;
            int localMax = 0;
            for (int i = 21; i < civ.size() - 1; ++i) {
                localMax = qMax(localMax, static_cast<int>(static_cast<quint8>(civ[i])));
            }
            maxAmp = qMax(maxAmp, localMax);
            maxLen = qMax(maxLen, civ.size() - 22);
            // Decode the embedded scope centre/start freq (bytes 10-14, LE BCD)
            // on a few frames to confirm the metadata tracks the VFO.
            // Run the REAL parser on a few frames to confirm end-to-end output.
            if (scopeFrames == 1 || scopeFrames == 250) {
                if (const auto frame = IcomCiv::parseFrame(civ)) {
                    IcomScope::ScopeAssembler asm2;
                    if (asm2.ingest(frame->payload) && asm2.complete()) {
                        const auto geom = IcomScope::scopeGeometry(asm2.header());
                        const auto bins = IcomScope::samplesToDbm(asm2.rawSamples());
                        float mn = 1e9f, mx = -1e9f;
                        for (float d : bins) { mn = qMin(mn, d); mx = qMax(mx, d); }
                        std::printf("SCOPE PARSE #%ld: %d bins, span %.4f–%.4f MHz, "
                                    "mode=%d oor=%d dBm[min=%.1f max=%.1f]\n",
                                    scopeFrames, bins.size(), geom.lowMhz, geom.highMhz,
                                    static_cast<int>(asm2.header().mode),
                                    asm2.header().outOfRange ? 1 : 0, mn, mx);
                        std::fflush(stdout);
                    } else {
                        std::printf("SCOPE PARSE #%ld: FAILED to assemble\n", scopeFrames);
                    }
                }
            }
            if (scopeFrames <= 3 || localMax > 0) {
                std::printf("SCOPE #%ld (%d B, %d wave bytes) peak-amp=%d  (running max amp=%d)\n",
                            scopeFrames, civ.size(), civ.size() - 22, localMax, maxAmp);
                std::fflush(stdout);
            }
            return;
        }
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
        std::printf("=== scope frames received: %ld ===\n", scopeFrames);
        transport.disconnectFromRadio();
        QTimer::singleShot(300, &app, [&] { app.quit(); });
    });
    return app.exec();
}
