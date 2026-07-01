#include "core/IcomBackend.h"

#include "core/IcomCivCodec.h"

#include <QString>

#include <cstdio>

// Freestanding test for IcomBackend's pure, socket-free mapping helpers.
//
// IcomBackend owns a QObject IcomUdpTransport with live UDP sockets, so we do
// NOT construct the backend here.  We exercise only the static helpers that
// carry the mapping logic: bandwidth -> filter slot, receiver -> scope stream
// id, and mode-string <-> CivMode.  No test framework; returns 0 on success,
// 1 on the first failure.

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "icom_backend_test: %s\n", message);
    return 1;
}

} // namespace

int main()
{
    using AetherSDR::IcomBackend;
    namespace Civ = AetherSDR::IcomCiv;

    // --- Filter-slot mapping: SSB (3.0 / 2.4 / 1.8 kHz nominal). ----------
    // Exact nominal widths land on their own slot.
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 3000) != Civ::kFilterFil1) {
        return fail("SSB 3000 Hz should map to FIL1");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 2400) != Civ::kFilterFil2) {
        return fail("SSB 2400 Hz should map to FIL2");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Lsb, 1800) != Civ::kFilterFil3) {
        return fail("LSB 1800 Hz should map to FIL3");
    }
    // Nearest-slot quantization for off-nominal widths.
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 2800) != Civ::kFilterFil1) {
        return fail("SSB 2800 Hz is closer to 3000 (FIL1) than 2400 (FIL2)");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 2000) != Civ::kFilterFil3) {
        return fail("SSB 2000 Hz is closer to 1800 (FIL3) than 2400 (FIL2)");
    }
    // A very wide request saturates to the widest slot.
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 10000) != Civ::kFilterFil1) {
        return fail("SSB 10000 Hz should saturate to the widest slot FIL1");
    }
    // A very narrow request saturates to the narrowest slot.
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, 50) != Civ::kFilterFil3) {
        return fail("SSB 50 Hz should saturate to the narrowest slot FIL3");
    }
    // Reversed / negative bandwidth clamps to 0 -> narrowest slot.
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, -500) != Civ::kFilterFil3) {
        return fail("negative bandwidth should clamp to narrowest slot FIL3");
    }

    // --- Filter-slot mapping: CW (1.2 / 0.5 / 0.25 kHz nominal). ----------
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Cw, 1200) != Civ::kFilterFil1) {
        return fail("CW 1200 Hz should map to FIL1");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Cw, 500) != Civ::kFilterFil2) {
        return fail("CW 500 Hz should map to FIL2");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::CwR, 250) != Civ::kFilterFil3) {
        return fail("CW-R 250 Hz should map to FIL3");
    }
    // A 2400 Hz SSB-shaped request in CW mode still maps to the CW widest slot,
    // proving mode-dependence of the mapping (not a shared table).
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Cw, 2400) != Civ::kFilterFil1) {
        return fail("CW 2400 Hz should saturate to CW widest slot FIL1");
    }

    // --- Filter-slot mapping: AM (9.0 / 6.0 / 3.0 kHz nominal). -----------
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Am, 9000) != Civ::kFilterFil1) {
        return fail("AM 9000 Hz should map to FIL1");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Am, 6000) != Civ::kFilterFil2) {
        return fail("AM 6000 Hz should map to FIL2");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Am, 3000) != Civ::kFilterFil3) {
        return fail("AM 3000 Hz should map to FIL3");
    }

    // --- Filter-slot mapping: FM (15 / 10 / 7 kHz nominal). ---------------
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Fm, 15000) != Civ::kFilterFil1) {
        return fail("FM 15000 Hz should map to FIL1");
    }
    if (IcomBackend::filterSlotForBandwidth(Civ::CivMode::Fm, 7000) != Civ::kFilterFil3) {
        return fail("FM 7000 Hz should map to FIL3");
    }

    // Every slot returned is a valid FIL1/2/3 byte for a spread of widths.
    for (int bw = 0; bw <= 20000; bw += 137) {
        const quint8 slot = IcomBackend::filterSlotForBandwidth(Civ::CivMode::Usb, bw);
        if (slot != Civ::kFilterFil1 && slot != Civ::kFilterFil2
            && slot != Civ::kFilterFil3) {
            return fail("filterSlotForBandwidth returned an out-of-range slot");
        }
    }

    // --- receiver -> scope stream id. ------------------------------------
    // Main receiver always the main stream, regardless of scope count.
    if (IcomBackend::scopeStreamIdForReceiver(0, 1) != 0u) {
        return fail("receiver 0 should always be scope stream 0 (main)");
    }
    if (IcomBackend::scopeStreamIdForReceiver(0, 2) != 0u) {
        return fail("receiver 0 on a dual-scope radio is still stream 0");
    }
    // Sub receiver on a dual-scope radio -> distinct stream 1.
    if (IcomBackend::scopeStreamIdForReceiver(1, 2) != 1u) {
        return fail("receiver 1 on a dual-scope radio should be stream 1 (sub)");
    }
    // Sub receiver on a single-scope radio collapses back to the main stream.
    if (IcomBackend::scopeStreamIdForReceiver(1, 1) != 0u) {
        return fail("receiver 1 on a single-scope radio should collapse to stream 0");
    }
    // Out-of-range receiver index also collapses to main on single scope.
    if (IcomBackend::scopeStreamIdForReceiver(5, 1) != 0u) {
        return fail("receiver 5 on a single-scope radio should be stream 0");
    }
    // Negative receiver index defensively maps to main.
    if (IcomBackend::scopeStreamIdForReceiver(-1, 2) != 0u) {
        return fail("negative receiver index should map to main stream 0");
    }

    // --- mode-string <-> CivMode round-trips. ----------------------------
    struct ModeVector {
        const char* text;
        Civ::CivMode mode;
    };
    const ModeVector modeVectors[] = {
        {"LSB", Civ::CivMode::Lsb},
        {"USB", Civ::CivMode::Usb},
        {"AM", Civ::CivMode::Am},
        {"CW", Civ::CivMode::Cw},
        {"CW-R", Civ::CivMode::CwR},
        {"RTTY", Civ::CivMode::Rtty},
        {"RTTY-R", Civ::CivMode::RttyR},
        {"FM", Civ::CivMode::Fm},
        {"WFM", Civ::CivMode::Wfm},
        {"DV", Civ::CivMode::Dv},
    };
    for (const ModeVector& v : modeVectors) {
        const Civ::CivMode parsed =
            IcomBackend::civModeFromString(QString::fromLatin1(v.text));
        if (parsed != v.mode) {
            return fail("civModeFromString parsed the wrong mode");
        }
        if (IcomBackend::civModeToString(v.mode) != QString::fromLatin1(v.text)) {
            return fail("civModeToString produced the wrong string");
        }
    }
    // Case-insensitive and whitespace-tolerant parsing.
    if (IcomBackend::civModeFromString(QStringLiteral("  usb ")) != Civ::CivMode::Usb) {
        return fail("civModeFromString should trim and upcase");
    }
    // CWR / RTTYR aliases (without the hyphen) resolve to the reverse modes.
    if (IcomBackend::civModeFromString(QStringLiteral("CWR")) != Civ::CivMode::CwR) {
        return fail("civModeFromString should accept the CWR alias");
    }
    if (IcomBackend::civModeFromString(QStringLiteral("RTTYR")) != Civ::CivMode::RttyR) {
        return fail("civModeFromString should accept the RTTYR alias");
    }
    // Unknown strings default to USB (matches the RX-first glue).
    if (IcomBackend::civModeFromString(QStringLiteral("bogus")) != Civ::CivMode::Usb) {
        return fail("civModeFromString should default unknown to USB");
    }

    return 0;
}
