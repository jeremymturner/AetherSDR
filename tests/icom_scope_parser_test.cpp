#include "core/IcomScopeParser.h"

#include <QByteArray>
#include <QVector>

#include <cmath>
#include <cstdio>

namespace {

bool nearlyEqual(float a, float b, float epsilon = 0.001f)
{
    return std::fabs(a - b) <= epsilon;
}

bool nearlyEqual(double a, double b, double epsilon = 1e-6)
{
    return std::fabs(a - b) <= epsilon;
}

int fail(const char* message)
{
    std::fprintf(stderr, "icom_scope_parser_test: %s\n", message);
    return 1;
}

// Local little-endian packed BCD encoder, duplicated so the test does not
// depend on the codec module. 14074000 Hz over 5 bytes -> 00 40 07 14 00.
QByteArray freqToLittleEndianBcd(quint64 hz, int numBytes = 5)
{
    QByteArray bcd(numBytes, '\0');
    for (int i = 0; i < numBytes; ++i) {
        const int units = static_cast<int>(hz % 10);
        hz /= 10;
        const int tens = static_cast<int>(hz % 10);
        hz /= 10;
        bcd[i] = static_cast<char>((tens << 4) | units);
    }
    return bcd;
}

// Build a first-division 0x27 payload (sub-command + division indicators +
// header + optional samples). mixes the documented provisional layout.
// Confirmed layout: subcmd, main/sub selector, current/total division, mode,
// two BCD freq fields, out-of-range flag, then samples.
QByteArray firstDivisionPayload(quint8 modeByte, int totalDivisions,
                                quint64 firstFreqHz, quint64 secondFreqHz,
                                const QByteArray& samples)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));            // sub-command
    payload.append(static_cast<char>(0x00));            // main/sub selector
    payload.append(static_cast<char>(0x01));            // current division = 1
    payload.append(static_cast<char>(totalDivisions));  // total divisions
    payload.append(static_cast<char>(modeByte));        // scope mode
    payload.append(freqToLittleEndianBcd(firstFreqHz));
    payload.append(freqToLittleEndianBcd(secondFreqHz));
    payload.append(static_cast<char>(0x00));            // out-of-range flag
    payload.append(samples);
    return payload;
}

// Build a continuation-division 0x27 payload.
QByteArray continuationPayload(int currentDivision, int totalDivisions,
                               const QByteArray& samples)
{
    QByteArray payload;
    payload.append(static_cast<char>(0x00));            // sub-command
    payload.append(static_cast<char>(0x00));            // main/sub selector
    payload.append(static_cast<char>(currentDivision));
    payload.append(static_cast<char>(totalDivisions));
    payload.append(samples);
    return payload;
}

} // namespace

int main()
{
    using namespace AetherSDR::IcomScope;

    // --- Center-mode first-division header parse ---------------------------
    const quint64 centerHz = 14100000;    // 14.1 MHz
    const quint64 spanHz = 200000;        // 200 kHz span
    const QByteArray centerFirst = firstDivisionPayload(
        kModeCenterByte, 3, centerHz, spanHz,
        QByteArray::fromHex("0080ff"));
    const std::optional<ScopeWaveformHeader> centerHeader =
        parseWaveformHeader(centerFirst);
    if (!centerHeader.has_value()
        || !centerHeader->valid
        || centerHeader->mode != ScopeMode::Center
        || !centerHeader->isFirstDivision
        || centerHeader->currentDivision != 1
        || centerHeader->totalDivisions != 3
        || centerHeader->centerOrLowFreqHz != centerHz
        || centerHeader->spanOrHighFreqHz != spanHz) {
        return fail("center-mode first-division header parse is wrong");
    }

    // Centre mode: the span field is the ±half-width, so total span is 2×.
    // centre 14.1 MHz, span field 200 kHz -> 13.9 .. 14.3 MHz.
    const ScopeGeometry centerGeometry = scopeGeometry(*centerHeader);
    if (!nearlyEqual(centerGeometry.lowMhz, 13.9)
        || !nearlyEqual(centerGeometry.highMhz, 14.3)) {
        return fail("center-mode geometry math is wrong");
    }

    // --- Fixed-mode first-division header parse ----------------------------
    const quint64 lowHz = 7000000;        // 7.0 MHz
    const quint64 highHz = 7300000;       // 7.3 MHz
    const QByteArray fixedFirst = firstDivisionPayload(
        kModeFixedByte, 1, lowHz, highHz, QByteArray());
    const std::optional<ScopeWaveformHeader> fixedHeader =
        parseWaveformHeader(fixedFirst);
    if (!fixedHeader.has_value()
        || !fixedHeader->valid
        || fixedHeader->mode != ScopeMode::Fixed
        || fixedHeader->centerOrLowFreqHz != lowHz
        || fixedHeader->spanOrHighFreqHz != highHz) {
        return fail("fixed-mode first-division header parse is wrong");
    }
    const ScopeGeometry fixedGeometry = scopeGeometry(*fixedHeader);
    if (!nearlyEqual(fixedGeometry.lowMhz, 7.0)
        || !nearlyEqual(fixedGeometry.highMhz, 7.3)) {
        return fail("fixed-mode geometry math is wrong");
    }

    // --- Continuation-division header parse --------------------------------
    const std::optional<ScopeWaveformHeader> contHeader =
        parseWaveformHeader(continuationPayload(2, 3, QByteArray(4, '\x40')));
    if (!contHeader.has_value()
        || !contHeader->valid
        || contHeader->isFirstDivision
        || contHeader->currentDivision != 2
        || contHeader->totalDivisions != 3
        || contHeader->mode != ScopeMode::Unknown) {
        return fail("continuation-division header parse is wrong");
    }

    // --- Malformed / partial input safety ----------------------------------
    if (parseWaveformHeader(QByteArray()).has_value()
        || parseWaveformHeader(QByteArray::fromHex("00")).has_value()
        || parseWaveformHeader(QByteArray::fromHex("0001")).has_value()) {
        return fail("too-short payloads must return nullopt");
    }
    // Wrong sub-command (byte 0 != 0x00).
    if (parseWaveformHeader(QByteArray::fromHex("01000103")).has_value()) {
        return fail("non-waveform sub-command must return nullopt");
    }
    // Zero / inverted division counts (subcmd, main/sub, curDiv, total).
    if (parseWaveformHeader(QByteArray::fromHex("00000003")).has_value()
        || parseWaveformHeader(QByteArray::fromHex("00000302")).has_value()) {
        return fail("invalid division counts must return nullopt");
    }
    // First division claiming header but truncated before the frequencies.
    if (parseWaveformHeader(QByteArray::fromHex("0000010100")).has_value()) {
        return fail("truncated first-division header must return nullopt");
    }
    // First division with an unknown mode byte.
    {
        QByteArray badMode = firstDivisionPayload(
            0x07, 1, centerHz, spanHz, QByteArray());
        if (parseWaveformHeader(badMode).has_value()) {
            return fail("unknown scope mode byte must return nullopt");
        }
    }
    // First division with a non-BCD frequency nibble.
    {
        QByteArray badBcd = firstDivisionPayload(
            kModeCenterByte, 1, centerHz, spanHz, QByteArray());
        badBcd[kProvisionalFirstFreqOffset] = static_cast<char>(0xAB);
        if (parseWaveformHeader(badBcd).has_value()) {
            return fail("non-BCD frequency nibble must return nullopt");
        }
    }
    // Invalid geometry for a continuation header stays zeroed.
    const ScopeGeometry contGeometry = scopeGeometry(*contHeader);
    if (!nearlyEqual(contGeometry.lowMhz, 0.0)
        || !nearlyEqual(contGeometry.highMhz, 0.0)) {
        return fail("continuation geometry should be zeroed");
    }

    // --- samplesToDbm range mapping ----------------------------------------
    // Amplitude bytes run 0x00 (bottom) to 0xA0 (160, top); 0xff clamps to top.
    const QByteArray sampleBytes = QByteArray::fromHex("00a08080ff");
    const QVector<float> dbm = samplesToDbm(sampleBytes, -20.0f, 100.0f);
    if (dbm.size() != 5
        || !nearlyEqual(dbm[0], -120.0f)                 // byte 0    -> ref-range
        || !nearlyEqual(dbm[1], -20.0f)                  // byte 0xA0 -> ref
        || !nearlyEqual(dbm[2], -120.0f + (128.0f / 160.0f) * 100.0f)  // 0x80
        || !nearlyEqual(dbm[4], -20.0f)) {               // 0xff clamps to ref
        return fail("samplesToDbm range mapping is wrong");
    }
    if (!samplesToDbm(QByteArray()).isEmpty()) {
        return fail("empty samples should map to empty dBm vector");
    }

    // --- Assembler: completion across three divisions ----------------------
    ScopeAssembler assembler;
    const QByteArray firstSamples = QByteArray::fromHex("0102");
    const QByteArray secondSamples = QByteArray::fromHex("0304");
    const QByteArray thirdSamples = QByteArray::fromHex("0506");
    if (!assembler.ingest(firstDivisionPayload(
            kModeCenterByte, 3, centerHz, spanHz, firstSamples))) {
        return fail("assembler should accept first division");
    }
    if (assembler.complete()) {
        return fail("assembler should not be complete after first division");
    }
    if (assembler.header().mode != ScopeMode::Center
        || assembler.header().totalDivisions != 3) {
        return fail("assembler should retain first-division header");
    }
    if (!assembler.ingest(continuationPayload(2, 3, secondSamples))
        || assembler.complete()) {
        return fail("assembler should accept second division, not complete");
    }
    if (!assembler.ingest(continuationPayload(3, 3, thirdSamples))
        || !assembler.complete()) {
        return fail("assembler should complete on final division");
    }
    if (assembler.rawSamples()
        != firstSamples + secondSamples + thirdSamples) {
        return fail("assembled raw samples are wrong");
    }

    // --- Assembler: out-of-order / duplicate divisions rejected ------------
    ScopeAssembler ooo;
    ooo.ingest(firstDivisionPayload(
        kModeCenterByte, 3, centerHz, spanHz, firstSamples));
    // Skipping straight to division 3 must be rejected (expected 2).
    if (ooo.ingest(continuationPayload(3, 3, thirdSamples))) {
        return fail("out-of-order division must be rejected");
    }
    // A mismatched total-division count must be rejected.
    if (ooo.ingest(continuationPayload(2, 4, secondSamples))) {
        return fail("mismatched total-division count must be rejected");
    }
    // Correct next division still accepted after the bad ones.
    if (!ooo.ingest(continuationPayload(2, 3, secondSamples))) {
        return fail("assembler should recover and accept correct division");
    }
    // A duplicate of the just-accepted division is rejected (expected 3 now).
    if (ooo.ingest(continuationPayload(2, 3, secondSamples))) {
        return fail("duplicate division must be rejected");
    }

    // --- Assembler: reset on a new first-division marker -------------------
    ScopeAssembler resetting;
    resetting.ingest(firstDivisionPayload(
        kModeCenterByte, 3, centerHz, spanHz, firstSamples));
    resetting.ingest(continuationPayload(2, 3, secondSamples));
    // A new first-division marker mid-sweep discards the partial sweep.
    if (!resetting.ingest(firstDivisionPayload(
            kModeFixedByte, 2, lowHz, highHz, thirdSamples))) {
        return fail("assembler should accept a restarting first division");
    }
    if (resetting.complete()
        || resetting.header().mode != ScopeMode::Fixed
        || resetting.rawSamples() != thirdSamples) {
        return fail("assembler did not reset on new first-division marker");
    }

    // --- Assembler: single-division sweep completes immediately ------------
    ScopeAssembler single;
    if (!single.ingest(firstDivisionPayload(
            kModeFixedByte, 1, lowHz, highHz, firstSamples))
        || !single.complete()
        || single.rawSamples() != firstSamples) {
        return fail("single-division sweep should complete immediately");
    }

    // --- Assembler: malformed payload ignored ------------------------------
    ScopeAssembler malformed;
    if (malformed.ingest(QByteArray::fromHex("0001"))
        || malformed.complete()
        || !malformed.rawSamples().isEmpty()) {
        return fail("assembler must ignore malformed payloads");
    }
    // A continuation before any first division is rejected.
    if (malformed.ingest(continuationPayload(2, 3, secondSamples))) {
        return fail("continuation before first division must be rejected");
    }

    return 0;
}
