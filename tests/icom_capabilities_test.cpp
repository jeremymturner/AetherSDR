#include "core/IcomRadioCapabilities.h"

#include <QString>
#include <QVector>

#include <cstdio>

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "icom_capabilities_test: %s\n", message);
    return 1;
}

} // namespace

int main()
{
    using namespace AetherSDR;

    // IC-705: WiFi, single RX/scope, no sat, HAS GPS, CI-V 0xA4.
    const IcomModelCaps ic705 = icomCapsFor(QStringLiteral("IC-705"));
    if (!ic705.isKnown
        || ic705.defaultCivAddress != static_cast<quint8>(0xA4)
        || ic705.primaryTransport != IcomTransport::WiFi
        || ic705.receiverCount != 1
        || ic705.scopeCount != 1
        || ic705.hasSatelliteMode
        || !ic705.hasGps) {
        return fail("IC-705 capabilities are wrong");
    }

    // IC-9700: Ethernet, 1 RX / 2 scopes, HAS satellite mode, no GPS,
    // CI-V 0xA2.
    const IcomModelCaps ic9700 = icomCapsFor(QStringLiteral("IC-9700"));
    if (!ic9700.isKnown
        || ic9700.defaultCivAddress != static_cast<quint8>(0xA2)
        || ic9700.primaryTransport != IcomTransport::Ethernet
        || ic9700.receiverCount != 1
        || ic9700.scopeCount != 2
        || !ic9700.hasSatelliteMode
        || ic9700.hasGps) {
        return fail("IC-9700 capabilities are wrong");
    }

    // IC-7610: Ethernet, 2 RX / 2 scopes, no sat, no GPS, CI-V 0x98.
    const IcomModelCaps ic7610 = icomCapsFor(QStringLiteral("IC-7610"));
    if (!ic7610.isKnown
        || ic7610.defaultCivAddress != static_cast<quint8>(0x98)
        || ic7610.primaryTransport != IcomTransport::Ethernet
        || ic7610.receiverCount != 2
        || ic7610.scopeCount != 2
        || ic7610.hasSatelliteMode
        || ic7610.hasGps) {
        return fail("IC-7610 capabilities are wrong");
    }

    // IC-7300MK2: Ethernet (the MK2 adds LAN; original 7300 was USB-only),
    // 1 RX / 1 scope, no sat, no GPS, CI-V 0x94.
    const IcomModelCaps ic7300mk2 = icomCapsFor(QStringLiteral("IC-7300MK2"));
    if (!ic7300mk2.isKnown
        || ic7300mk2.defaultCivAddress != static_cast<quint8>(0x94)
        || ic7300mk2.primaryTransport != IcomTransport::Ethernet
        || ic7300mk2.receiverCount != 1
        || ic7300mk2.scopeCount != 1
        || ic7300mk2.hasSatelliteMode
        || ic7300mk2.hasGps) {
        return fail("IC-7300MK2 capabilities are wrong");
    }

    // Case-insensitive lookup resolves to the same entry.
    const IcomModelCaps lower = icomCapsFor(QStringLiteral("ic-7610"));
    if (!lower.isKnown
        || lower.modelKey != QStringLiteral("IC-7610")
        || lower.defaultCivAddress != static_cast<quint8>(0x98)) {
        return fail("case-insensitive lookup is wrong");
    }

    // Substring lookup ignores vendor/serial suffixes.
    const IcomModelCaps suffixed =
        icomCapsFor(QStringLiteral("IC-7610 (Serial 123)"));
    if (!suffixed.isKnown
        || suffixed.modelKey != QStringLiteral("IC-7610")
        || suffixed.defaultCivAddress != static_cast<quint8>(0x98)) {
        return fail("substring/suffix lookup is wrong");
    }

    // Unknown model degrades gracefully with isKnown=false and safe defaults.
    const IcomModelCaps unknown = icomCapsFor(QStringLiteral("IC-9999"));
    if (unknown.isKnown
        || unknown.receiverCount != 0
        || unknown.scopeCount != 0
        || unknown.hasSatelliteMode
        || unknown.hasGps
        || unknown.defaultCivAddress != static_cast<quint8>(0x00)) {
        return fail("unknown model should report isKnown=false with safe defaults");
    }

    // Empty key is also treated as unknown.
    if (icomCapsFor(QString()).isKnown) {
        return fail("empty model key should be unknown");
    }

    // allIcomModels() returns exactly the four documented radios, each known.
    const QVector<IcomModelCaps> all = allIcomModels();
    if (all.size() != 4) {
        return fail("allIcomModels() should return four models");
    }
    for (const IcomModelCaps& caps : all) {
        if (!caps.isKnown || caps.modelKey.isEmpty()) {
            return fail("every model in allIcomModels() should be known and keyed");
        }
    }

    return 0;
}
