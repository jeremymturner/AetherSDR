#pragma once

// Icom CI-V 0x27 scope "waveform data" parser (public protocol only).
//
// Derived from Icom's public CI-V Reference Guides; no GPL wfview code was
// referenced. See docs/icom-cleanroom-design.md and issue #6.
//
// In the spirit of KiwiSdrProtocol and IcomCivCodec: pure, boundary-checked
// free functions plus small POD structs, no QObject, no ownership, no I/O.
// The one exception is ScopeAssembler, a tiny stateful accumulator (a plain
// class, still no QObject) that reassembles a full sweep from the multiple
// CI-V frames the radio splits it across.
//
// FRAMING BOUNDARY: this module operates on the ALREADY-DE-FRAMED CI-V
// payload for command 0x27 — the bytes AFTER the 0x27 command byte and
// BEFORE the 0xFD terminator (i.e. the sub-command byte followed by data).
// The FE FE <to> <from> 0x27 ... FD wire framing is handled by the separate
// IcomCiv codec; do NOT re-implement it here.
//
// PROVISIONAL CONSTANTS: the CI-V 0x27 waveform structure is documented, but
// several exact values (division count, per-model sample count, the dBm
// calibration curve, and a couple of header byte offsets) are model-specific
// and/or empirical. Everything provisional is named with a kProvisional...
// constant and flagged with a "PROVISIONAL" comment so it can be confirmed
// against a first-party capture (issue #6). No undocumented magic numbers.

#include <QByteArray>
#include <QVector>

#include <QtGlobal>

#include <optional>

namespace AetherSDR::IcomScope {

// Scope sub-command carried in the first payload byte of a 0x27 message.
// 0x00 = "waveform data" is the only sub-command this module parses.
inline constexpr quint8 kSubCmdWaveformData = 0x00;

// Scope display mode byte (public CI-V spec: 0 = center, 1 = fixed).
inline constexpr quint8 kModeCenterByte = 0x00;
inline constexpr quint8 kModeFixedByte = 0x01;

// Width of an edge/center/span frequency field in the first-division header.
// Icom sends scope edge frequencies as little-endian packed BCD, same as the
// operating-frequency field: 5 bytes = 10 BCD digits = 1 Hz resolution.
inline constexpr int kFreqBcdBytes = 5;

// --- PROVISIONAL layout constants (confirm against a first-party capture) ---
//
// The public docs describe the field ORDER of a waveform message but the
// exact byte offsets and the total division count vary by model and by the
// connection type (USB CDC vs LAN). The values below encode the documented
// structure with the most commonly reported layout; they are flagged
// PROVISIONAL and MUST be verified per docs/icom-cleanroom-design.md.

// CONFIRMED byte offsets within a de-framed 0x27 payload (verified against a
// real IC-705 over LAN — the earlier provisional layout was missing the
// main/sub selector byte and the out-of-range flag):
//   [0]        sub-command (0x00 waveform data)
//   [1]        main/sub scope selector (0 = main, 1 = sub)
//   [2]        current-division index (BCD, 1-based)
//   [3]        total-division count  (BCD)
//   For the FIRST division only, a header follows:
//   [4]        scope mode (0 = center, 1 = fixed, 2 = scroll-C, 3 = scroll-F)
//   [5..9]     center (Center mode) or low edge (Fixed mode) freq, LE BCD
//   [10..14]   span  (Center mode) or high edge (Fixed mode) freq, LE BCD
//   [15]       out-of-range flag (1 = signal out of scope range)
//   [16..]     amplitude sample bytes (0x00 bottom .. 0xA0 top)
// For subsequent divisions the amplitude sample bytes start right after the
// division indicators (offset kProvisionalSampleOffsetContinuation).
// Over LAN the whole sweep arrives as a single "division 1 of 1" frame.
inline constexpr int kProvisionalSubCmdOffset = 0;
inline constexpr int kProvisionalMainSubOffset = 1;
inline constexpr int kProvisionalCurrentDivisionOffset = 2;
inline constexpr int kProvisionalTotalDivisionsOffset = 3;
inline constexpr int kProvisionalModeOffset = 4;
inline constexpr int kProvisionalFirstFreqOffset = 5;
inline constexpr int kProvisionalSecondFreqOffset =
    kProvisionalFirstFreqOffset + kFreqBcdBytes;               // 10
inline constexpr int kProvisionalOorOffset =
    kProvisionalSecondFreqOffset + kFreqBcdBytes;             // 15
inline constexpr int kProvisionalHeaderEndOffset =
    kProvisionalOorOffset + 1;                                // 16
inline constexpr int kProvisionalSampleOffsetContinuation =
    kProvisionalTotalDivisionsOffset + 1;                     // 4

// PROVISIONAL: the first division index Icom uses is 1 (1-based counting).
inline constexpr int kProvisionalFirstDivisionIndex = 1;

// PROVISIONAL: sanity ceiling on the total-division count so a corrupt byte
// cannot make the assembler wait forever / allocate absurdly.
inline constexpr int kProvisionalMaxDivisions = 64;

// PROVISIONAL: typical full-sweep sample counts (~475 or ~689 points depending
// on model / connection). Exposed for reference; the assembler does not force
// a specific count — it concatenates whatever the divisions carry.
inline constexpr int kProvisionalSampleCountUsb = 475;
inline constexpr int kProvisionalSampleCountLan = 689;

// Amplitude sample bytes span 0..kProvisionalMaxSampleByte. Confirmed against a
// real radio: Icom scope amplitude bytes run 0x00 (display bottom) to 0xA0 (160,
// display top). The real byte->dBm curve is still model-specific/empirical, so
// samplesToDbm takes ref/range params and maps linearly across this range.
inline constexpr int kProvisionalMaxSampleByte = 0xA0;  // 160

// Default reference/range for samplesToDbm when a caller has no calibration.
// PROVISIONAL: pending a first-party capture these are display placeholders,
// NOT calibrated dBm. -20 dBm top / 100 dB range mirrors a typical scope span.
inline constexpr float kProvisionalDefaultRefDbm = -20.0f;
inline constexpr float kProvisionalDefaultRangeDb = 100.0f;

enum class ScopeMode {
    Center,
    Fixed,
    Unknown,
};

// Scope mode bytes (public CI-V spec). Scroll-C/Scroll-F behave like Center/
// Fixed for geometry purposes.
inline constexpr quint8 kModeScrollCByte = 0x02;
inline constexpr quint8 kModeScrollFByte = 0x03;

// Parsed first-division header (or the division indicators of a continuation
// frame, in which case only the division fields are populated).
struct ScopeWaveformHeader {
    ScopeMode mode{ScopeMode::Unknown};
    int currentDivision{0};
    int totalDivisions{0};
    bool isFirstDivision{false};
    // Center mode: center frequency / span, in Hz.
    // Fixed  mode: low edge / high edge, in Hz.
    // (See ScopeGeometry for the derived low/high MHz pair.)
    quint64 centerOrLowFreqHz{0};
    quint64 spanOrHighFreqHz{0};
    // True when the radio flags the signal as outside the scope's amplitude
    // range for this sweep (the waveform bytes are meaningless / zeroed).
    bool outOfRange{false};
    bool valid{false};
};

// Sweep frequency geometry derived from a first-division header.
struct ScopeGeometry {
    double lowMhz{0.0};
    double highMhz{0.0};
};

// Parse the division indicators (and, for the first division, the header) out
// of an already-de-framed CI-V 0x27 payload. Boundary-checks every access and
// returns nullopt on any malformed/too-short input. Never indexes past the
// buffer.
std::optional<ScopeWaveformHeader> parseWaveformHeader(
    const QByteArray& civ27Payload);

// Compute the low/high sweep edges (MHz) from a parsed header.
//   Center mode: low = center - span/2, high = center + span/2
//   Fixed  mode: low/high are the header edges directly
// Returns a zeroed geometry for an invalid header.
ScopeGeometry scopeGeometry(const ScopeWaveformHeader& header);

// Map raw 0..255 amplitude sample bytes linearly onto [refDbm - rangeDb,
// refDbm]: byte 0 -> refDbm - rangeDb, byte 255 -> refDbm.
//
// PROVISIONAL calibration: the true per-model byte->dBm curve is empirical and
// pending a first-party capture (issue #6). This linear map is a documented
// placeholder; the ref/range parameters exist so a caller can plug in a
// calibrated span once captured.
QVector<float> samplesToDbm(
    const QByteArray& rawSamples,
    float refDbm = kProvisionalDefaultRefDbm,
    float rangeDb = kProvisionalDefaultRangeDb);

// Stateful accumulator that reassembles a full sweep from successive division
// payloads. Plain class (no QObject). Feed each de-framed 0x27 payload to
// ingest(); when the final division arrives, complete() is true and
// rawSamples() returns the concatenated amplitude bytes.
//
// Defensive by design: a first-division marker always resets the accumulator
// (so a fresh sweep recovers from a dropped/garbled previous one), and
// out-of-order or duplicate divisions are dropped rather than misplaced.
class ScopeAssembler {
public:
    ScopeAssembler() = default;

    // Ingest one de-framed 0x27 payload. Returns true if the payload was
    // accepted as the next expected division (and thus advanced state);
    // returns false for malformed, out-of-order, or duplicate frames.
    bool ingest(const QByteArray& civ27Payload);

    // True once every division of the current sweep has been assembled.
    bool complete() const { return m_complete; }

    // The concatenated amplitude sample bytes for the assembled sweep.
    // Empty until at least one division has been accepted.
    QByteArray rawSamples() const { return m_samples; }

    // Header of the sweep currently being assembled (from its first division).
    const ScopeWaveformHeader& header() const { return m_header; }

    // Discard any partially-assembled sweep.
    void reset();

private:
    ScopeWaveformHeader m_header{};
    QByteArray m_samples;
    int m_expectedDivision{0};
    int m_totalDivisions{0};
    bool m_complete{false};
    bool m_started{false};
};

} // namespace AetherSDR::IcomScope
