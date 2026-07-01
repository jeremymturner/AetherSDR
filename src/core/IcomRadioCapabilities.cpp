// IcomRadioCapabilities.cpp — per-model Icom capability table.
//
// All data in this file is transcribed from PUBLIC Icom manufacturer
// specifications (published spec sheets / operating manuals). It contains
// no code or data derived from GPL projects such as wfview. Implements the
// data portion of GitHub issue #9.

#include "core/IcomRadioCapabilities.h"

#include <QString>
#include <QVector>

#include <QtGlobal>

namespace AetherSDR {

namespace {

// The static capability table. Frequencies are stored in Hz as doubles.
// TX power is the maximum rated output (see per-model notes below).
const QVector<IcomModelCaps>& icomModelTable()
{
    static const QVector<IcomModelCaps> kModels = {
        // IC-705: QRP portable HF/50/144/430 SDR transceiver with built-in
        // GPS and WiFi/Bluetooth. Single receiver, single scope. Rated
        // 10 W with external DC (5 W on the internal battery) — record the
        // 10 W maximum. RX coverage ~30 kHz to 470 MHz.
        IcomModelCaps{
            QStringLiteral("IC-705"),
            QStringLiteral("Icom IC-705"),
            static_cast<quint8>(0xA4),
            IcomTransport::WiFi,
            /*receiverCount=*/1,
            /*scopeCount=*/1,
            /*hasSatelliteMode=*/false,
            /*hasGps=*/true,
            /*txPowerWattsMax=*/10.0,
            /*freqMinHz=*/30.0e3,
            /*freqMaxHz=*/470.0e6,
        },
        // IC-9700: VHF/UHF/1.2 GHz all-mode with main + sub receiver and
        // dual (main/sub) spectrum scopes. Supports satellite/duplex mode.
        // No built-in GPS. Rated TX differs per band (144: 100 W, 430:
        // 75 W, 1200: 10 W) — record the 100 W maximum. Coverage spans the
        // 144/430/1200 MHz bands; store min/max bracketing 144 MHz–1.3 GHz.
        IcomModelCaps{
            QStringLiteral("IC-9700"),
            QStringLiteral("Icom IC-9700"),
            static_cast<quint8>(0xA2),
            IcomTransport::Ethernet,
            /*receiverCount=*/1,
            /*scopeCount=*/2,
            /*hasSatelliteMode=*/true,
            /*hasGps=*/false,
            /*txPowerWattsMax=*/100.0,
            /*freqMinHz=*/144.0e6,
            /*freqMaxHz=*/1300.0e6,
        },
        // IC-7610: HF/50 MHz SDR base station with two fully independent
        // receivers and dual spectrum scopes. No satellite mode, no GPS.
        // Rated 100 W. RX coverage 30 kHz to 60 MHz.
        IcomModelCaps{
            QStringLiteral("IC-7610"),
            QStringLiteral("Icom IC-7610"),
            static_cast<quint8>(0x98),
            IcomTransport::Ethernet,
            /*receiverCount=*/2,
            /*scopeCount=*/2,
            /*hasSatelliteMode=*/false,
            /*hasGps=*/false,
            /*txPowerWattsMax=*/100.0,
            /*freqMinHz=*/30.0e3,
            /*freqMaxHz=*/60.0e6,
        },
        // IC-7300MK2: HF/50/70 MHz SDR transceiver, single receiver and a
        // single spectrum scope. The MK2 revision adds a LAN/Ethernet port
        // for network control — NOTE the ORIGINAL IC-7300 was USB-only and
        // had no Ethernet, so only the MK2 uses IcomTransport::Ethernet
        // here. No satellite mode, no GPS. Rated 100 W. RX coverage 30 kHz
        // to 74.8 MHz (70 MHz segment is region-dependent).
        IcomModelCaps{
            QStringLiteral("IC-7300MK2"),
            QStringLiteral("Icom IC-7300MK2"),
            static_cast<quint8>(0x94),
            IcomTransport::Ethernet,
            /*receiverCount=*/1,
            /*scopeCount=*/1,
            /*hasSatelliteMode=*/false,
            /*hasGps=*/false,
            /*txPowerWattsMax=*/100.0,
            /*freqMinHz=*/30.0e3,
            /*freqMaxHz=*/74.8e6,
        },
    };
    return kModels;
}

} // namespace

IcomModelCaps icomCapsFor(const QString& modelKey)
{
    const QString needle = modelKey.trimmed().toLower();
    if (!needle.isEmpty()) {
        for (const IcomModelCaps& caps : icomModelTable()) {
            // Case-insensitive substring match in both directions so that
            // either the query contains the model key (e.g.
            // "IC-7610 (Serial 123)") or the model key contains the query
            // (e.g. an exact "ic-7610") resolves to the entry.
            const QString key = caps.modelKey.toLower();
            if (needle.contains(key) || key.contains(needle)) {
                return caps;
            }
        }
    }

    // Unknown model: return safe, all-quiet defaults with isKnown=false.
    IcomModelCaps unknown{};
    unknown.modelKey = modelKey;
    unknown.displayName = modelKey.isEmpty()
                              ? QStringLiteral("Unknown Icom radio")
                              : modelKey;
    unknown.defaultCivAddress = static_cast<quint8>(0x00);
    unknown.primaryTransport = IcomTransport::Usb;
    unknown.receiverCount = 0;
    unknown.scopeCount = 0;
    unknown.hasSatelliteMode = false;
    unknown.hasGps = false;
    unknown.txPowerWattsMax = 0.0;
    unknown.freqMinHz = 0.0;
    unknown.freqMaxHz = 0.0;
    unknown.isKnown = false;
    return unknown;
}

QVector<IcomModelCaps> allIcomModels()
{
    return icomModelTable();
}

} // namespace AetherSDR
