#include "IcomScopeParser.h"

#include <algorithm>

namespace AetherSDR::IcomScope {
namespace {

// Decode a little-endian packed BCD frequency field of exactly `numBytes`
// bytes starting at `offset`. Mirrors IcomCiv::bcdToFreq (least-significant
// pair first), but kept local so this module has no cross-module dependency.
// Returns nullopt on any non-BCD nibble or an out-of-range slice.
std::optional<quint64> readLittleEndianBcd(const QByteArray& payload,
                                           int offset, int numBytes)
{
    if (offset < 0 || numBytes <= 0
        || offset + numBytes > payload.size()) {
        return std::nullopt;
    }

    quint64 hz = 0;
    // Walk most-significant byte first (highest index), folding each pair in.
    for (int i = offset + numBytes - 1; i >= offset; --i) {
        const auto byte = static_cast<quint8>(payload[i]);
        const int tens = (byte >> 4) & 0x0f;
        const int units = byte & 0x0f;
        if (tens > 9 || units > 9) {
            return std::nullopt;
        }
        hz = (hz * 100) + static_cast<quint64>((tens * 10) + units);
    }
    return hz;
}

ScopeMode modeFromByte(quint8 modeByte)
{
    switch (modeByte) {
    case kModeCenterByte:
    case kModeScrollCByte:   // scroll-centre behaves like centre for geometry
        return ScopeMode::Center;
    case kModeFixedByte:
    case kModeScrollFByte:   // scroll-fixed behaves like fixed for geometry
        return ScopeMode::Fixed;
    default:
        return ScopeMode::Unknown;
    }
}

// Decode a packed-BCD count byte (e.g. 0x11 -> 11), as Icom encodes the
// division sequence numbers. Non-BCD nibbles fall back to the raw value.
int bcdCount(quint8 b)
{
    const int hi = (b >> 4) & 0x0f;
    const int lo = b & 0x0f;
    if (hi > 9 || lo > 9) {
        return b;
    }
    return hi * 10 + lo;
}

} // namespace

std::optional<ScopeWaveformHeader> parseWaveformHeader(
    const QByteArray& civ27Payload)
{
    // Minimum: sub-command + current-division + total-division indicators.
    if (civ27Payload.size()
        <= kProvisionalTotalDivisionsOffset) {
        return std::nullopt;
    }

    // Only the waveform-data sub-command is understood here.
    if (static_cast<quint8>(civ27Payload[kProvisionalSubCmdOffset])
        != kSubCmdWaveformData) {
        return std::nullopt;
    }

    ScopeWaveformHeader header;
    header.currentDivision = bcdCount(static_cast<quint8>(
        civ27Payload[kProvisionalCurrentDivisionOffset]));
    header.totalDivisions = bcdCount(static_cast<quint8>(
        civ27Payload[kProvisionalTotalDivisionsOffset]));

    // A zero or absurd division count is malformed.
    if (header.currentDivision < kProvisionalFirstDivisionIndex
        || header.totalDivisions < kProvisionalFirstDivisionIndex
        || header.totalDivisions > kProvisionalMaxDivisions
        || header.currentDivision > header.totalDivisions) {
        return std::nullopt;
    }

    header.isFirstDivision =
        (header.currentDivision == kProvisionalFirstDivisionIndex);

    if (!header.isFirstDivision) {
        // Continuation frames carry no header fields — only the division
        // indicators are meaningful. Mode/frequency stay at their defaults.
        header.mode = ScopeMode::Unknown;
        header.valid = true;
        return header;
    }

    // First division: a mode byte and two BCD frequency fields must fit.
    if (civ27Payload.size() < kProvisionalHeaderEndOffset) {
        return std::nullopt;
    }

    header.mode = modeFromByte(
        static_cast<quint8>(civ27Payload[kProvisionalModeOffset]));
    if (header.mode == ScopeMode::Unknown) {
        return std::nullopt;
    }

    const std::optional<quint64> firstFreq = readLittleEndianBcd(
        civ27Payload, kProvisionalFirstFreqOffset, kFreqBcdBytes);
    const std::optional<quint64> secondFreq = readLittleEndianBcd(
        civ27Payload, kProvisionalSecondFreqOffset, kFreqBcdBytes);
    if (!firstFreq.has_value() || !secondFreq.has_value()) {
        return std::nullopt;
    }

    header.centerOrLowFreqHz = *firstFreq;
    header.spanOrHighFreqHz = *secondFreq;
    header.outOfRange =
        (static_cast<quint8>(civ27Payload[kProvisionalOorOffset]) != 0);
    header.valid = true;
    return header;
}

ScopeGeometry scopeGeometry(const ScopeWaveformHeader& header)
{
    ScopeGeometry geometry;
    if (!header.valid || !header.isFirstDivision) {
        return geometry;
    }

    constexpr double kHzPerMhz = 1000000.0;
    const double first =
        static_cast<double>(header.centerOrLowFreqHz) / kHzPerMhz;
    const double second =
        static_cast<double>(header.spanOrHighFreqHz) / kHzPerMhz;

    switch (header.mode) {
    case ScopeMode::Center: {
        // first = centre, second = the ±half-span (confirmed against a real
        // radio + wfview): the display runs centre-span .. centre+span, so the
        // total width is 2×span.
        geometry.lowMhz = first - second;
        geometry.highMhz = first + second;
        break;
    }
    case ScopeMode::Fixed:
        // first = low edge, second = high edge.
        geometry.lowMhz = first;
        geometry.highMhz = second;
        break;
    case ScopeMode::Unknown:
        break;
    }
    return geometry;
}

QVector<float> samplesToDbm(const QByteArray& rawSamples, float refDbm,
                            float rangeDb)
{
    QVector<float> dbm;
    dbm.reserve(static_cast<int>(rawSamples.size()));

    // byte 0 -> floor (refDbm - rangeDb), byte kProvisionalMaxSampleByte
    // -> refDbm. rangeDb <= 0 collapses the scale onto refDbm.
    const float floorDbm = refDbm - rangeDb;
    const float divisor = static_cast<float>(kProvisionalMaxSampleByte);
    for (const char raw : rawSamples) {
        const float level = static_cast<float>(static_cast<quint8>(raw))
            / divisor;
        dbm.append(floorDbm + (std::clamp(level, 0.0f, 1.0f) * rangeDb));
    }
    return dbm;
}

void ScopeAssembler::reset()
{
    m_header = ScopeWaveformHeader{};
    m_samples.clear();
    m_expectedDivision = 0;
    m_totalDivisions = 0;
    m_complete = false;
    m_started = false;
}

bool ScopeAssembler::ingest(const QByteArray& civ27Payload)
{
    const std::optional<ScopeWaveformHeader> parsed =
        parseWaveformHeader(civ27Payload);
    if (!parsed.has_value()) {
        return false;
    }
    const ScopeWaveformHeader& header = *parsed;

    // A first-division marker always starts a fresh sweep, discarding any
    // partially-assembled previous one (defensive recovery from drops).
    if (header.isFirstDivision) {
        reset();
        m_header = header;
        m_totalDivisions = header.totalDivisions;
        m_started = true;
        m_expectedDivision = kProvisionalFirstDivisionIndex;
        const int sampleOffset = kProvisionalHeaderEndOffset;
        if (civ27Payload.size() > sampleOffset) {
            m_samples.append(civ27Payload.mid(sampleOffset));
        }
        m_expectedDivision = kProvisionalFirstDivisionIndex + 1;
        m_complete = (m_totalDivisions == kProvisionalFirstDivisionIndex);
        return true;
    }

    // Continuation frame: only valid once a sweep has started, must match the
    // sweep's total-division count, and must be the next expected division.
    if (!m_started
        || header.totalDivisions != m_totalDivisions
        || header.currentDivision != m_expectedDivision) {
        return false;
    }

    const int sampleOffset = kProvisionalSampleOffsetContinuation;
    if (civ27Payload.size() > sampleOffset) {
        m_samples.append(civ27Payload.mid(sampleOffset));
    }
    m_expectedDivision += 1;
    if (header.currentDivision == m_totalDivisions) {
        m_complete = true;
    }
    return true;
}

} // namespace AetherSDR::IcomScope
