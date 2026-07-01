#pragma once

#include <QString>

namespace AetherSDR {

// Project-wide discriminator for which radio protocol backend is in use.
// Historically AetherSDR was FlexRadio-only and branched on `m_model` string
// contents; this enum is the typed replacement introduced with the
// RadioBackend abstraction (issue #2 / epic #1).  Kiwi is included because the
// KiwiSDR path is already a distinct non-Flex source; Unknown is the
// forward-compatible default for radios released after this build.
enum class RadioType {
    Flex,
    Icom,
    Kiwi,
    Unknown
};

inline QString radioTypeName(RadioType t)
{
    switch (t) {
    case RadioType::Flex:
        return QStringLiteral("Flex");
    case RadioType::Icom:
        return QStringLiteral("Icom");
    case RadioType::Kiwi:
        return QStringLiteral("Kiwi");
    case RadioType::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
}

} // namespace AetherSDR
