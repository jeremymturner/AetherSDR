#pragma once

#include "RadioType.h"  // AetherSDR::RadioType (shared with the RadioBackend seam, #2)

#include <QString>
#include <QVector>

#include <QtGlobal>

namespace AetherSDR {

// The physical/logical link an Icom radio uses for its network-capable
// remote interface (the transport AetherSDR would open by default).
enum class IcomTransport {
    Ethernet,
    WiFi,
    Usb,
};

// Per-model Icom capability record.  All values are drawn from public
// Icom manufacturer specifications (see the .cpp header).  Mirrors the
// ModelCapabilities pattern in src/models/ModelCapabilities.h: a plain
// value struct of flags/specs, resolved by a free lookup function, with
// an `isKnown` flag so unknown models degrade gracefully instead of
// silently masquerading as a real radio.
struct IcomModelCaps {
    QString modelKey;          // Canonical key, e.g. "IC-7610"
    QString displayName;       // Human-facing name
    quint8 defaultCivAddress;  // Default CI-V address, e.g. 0x94
    IcomTransport primaryTransport;
    int receiverCount;         // Independent RX (scopes)
    int scopeCount;
    bool hasSatelliteMode;     // e.g. IC-9700
    bool hasGps;               // e.g. IC-705
    double txPowerWattsMax;    // Max rated TX power in watts
    double freqMinHz;          // RX coverage low, Hz
    double freqMaxHz;          // RX coverage high, Hz
    bool isKnown{true};
};

// Returns capabilities for the given model key.  Uses a case-insensitive
// substring match (like capabilitiesFor in ModelCapabilities.h) so vendor
// suffixes such as "IC-7610 (Serial 123)" still resolve to the 7610 entry.
// Unknown keys return a struct with isKnown=false and safe defaults —
// forward-compatible for radios released after this build.
IcomModelCaps icomCapsFor(const QString& modelKey);

// Returns every Icom model currently described by this table.
QVector<IcomModelCaps> allIcomModels();

} // namespace AetherSDR
