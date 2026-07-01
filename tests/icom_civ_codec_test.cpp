#include "core/IcomCivCodec.h"

#include <QByteArray>

#include <cstdio>

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "icom_civ_codec_test: %s\n", message);
    return 1;
}

struct FreqVector {
    quint64 hz;
    const char* bcdHex;   // 5-byte little-endian BCD, as hex
};

struct FrameVector {
    quint8 toAddr;
    quint8 fromAddr;
    quint8 cmd;
    const char* payloadHex;   // may be empty
    const char* frameHex;     // full on-wire frame
};

} // namespace

int main()
{
    using namespace AetherSDR::IcomCiv;

    // --- BCD round-trips (5 bytes = 10 digits, 1 Hz resolution). ---------
    const FreqVector freqVectors[] = {
        {0ull, "0000000000"},
        {1ull, "0100000000"},
        {7074000ull, "0040070700"},
        {14074000ull, "0040071400"},
        {148000000ull, "0000004801"},
        {1296100000ull, "0000109612"},
        {9999999999ull, "9999999999"},
    };
    for (const FreqVector& vector : freqVectors) {
        const QByteArray expected = QByteArray::fromHex(vector.bcdHex);
        const QByteArray encoded = freqToBcd(vector.hz);
        if (encoded.size() != kFrequencyBcdBytes) {
            return fail("freqToBcd should produce 5 bytes by default");
        }
        if (encoded != expected) {
            return fail("freqToBcd little-endian BCD packing is wrong");
        }
        if (bcdToFreq(encoded) != vector.hz) {
            return fail("bcdToFreq round-trip is wrong");
        }
    }

    // Spec anchor: 14.074 MHz must be exactly 00 40 07 14 00.
    if (freqToBcd(14074000ull) != QByteArray::fromHex("0040071400")) {
        return fail("14.074 MHz BCD does not match the documented ordering");
    }

    // Custom width still round-trips (3 bytes = 6 digits).
    if (freqToBcd(123456ull, 3) != QByteArray::fromHex("563412")
        || bcdToFreq(QByteArray::fromHex("563412")) != 123456ull) {
        return fail("3-byte BCD width round-trip is wrong");
    }

    // Non-BCD nibbles decode to 0 (defensive) rather than garbage.
    if (bcdToFreq(QByteArray::fromHex("00ab0000")) != 0ull) {
        return fail("bcdToFreq should reject non-BCD nibbles");
    }

    // --- Frame encode + parse round-trips. -------------------------------
    const FrameVector frameVectors[] = {
        {kAddrIc7300, kControllerAddress, kCmdReadFrequency, "",
         "fefe94e003fd"},
        {kAddrIc9700, kControllerAddress, kCmdReadMode, "",
         "fefea2e004fd"},
        {kAddrIc705, kControllerAddress, kCmdSetMode, "0301",
         "fefea4e0060301fd"},
        {kControllerAddress, kAddrIc7610, kCmdReadFrequency, "0040071400",
         "fefee098030040071400fd"},
    };
    for (const FrameVector& vector : frameVectors) {
        const QByteArray payload = QByteArray::fromHex(vector.payloadHex);
        const QByteArray encoded =
            encodeFrame(vector.toAddr, vector.fromAddr, vector.cmd, payload);
        if (encoded != QByteArray::fromHex(vector.frameHex)) {
            return fail("encodeFrame byte layout is wrong");
        }
        const auto parsed = parseFrame(encoded);
        if (!parsed) {
            return fail("parseFrame rejected a well-formed frame");
        }
        if (parsed->toAddr != vector.toAddr
            || parsed->fromAddr != vector.fromAddr
            || parsed->cmd != vector.cmd
            || parsed->payload != payload) {
            return fail("parseFrame round-trip fields are wrong");
        }
    }

    // --- Malformed-frame rejection. --------------------------------------
    if (parseFrame(QByteArray())) {
        return fail("empty input should be rejected");
    }
    if (parseFrame(QByteArray::fromHex("fefe94e0fd"))) {
        return fail("too-short frame (5 bytes) should be rejected");
    }
    if (parseFrame(QByteArray::fromHex("fe0094e003fd"))) {
        return fail("missing second preamble byte should be rejected");
    }
    if (parseFrame(QByteArray::fromHex("00fe94e003fd"))) {
        return fail("missing first preamble byte should be rejected");
    }
    if (parseFrame(QByteArray::fromHex("fefe94e003ff"))) {
        return fail("missing FD terminator should be rejected");
    }
    if (parseFrame(QByteArray::fromHex("fefe94e003"))) {
        return fail("truncated frame (no terminator) should be rejected");
    }
    // A frame whose "to"/"from" position is itself the terminator implies a
    // truncated/garbled frame that merely happens to end in FD. This 6-byte
    // input passes the length and preamble/terminator checks, so it exercises
    // the to/from==FD guard specifically.
    if (parseFrame(QByteArray::fromHex("fefefdfdfdfd"))) {
        return fail("degenerate FE FE with FD address bytes should be rejected");
    }

    // --- Command builders: byte-exact assertions. ------------------------
    if (setFrequency(kAddrIc7300, 14074000ull)
            != QByteArray::fromHex("fefe94e0050040071400fd")) {
        return fail("setFrequency byte layout is wrong");
    }
    if (readFrequency(kAddrIc7300)
            != QByteArray::fromHex("fefe94e003fd")) {
        return fail("readFrequency byte layout is wrong");
    }
    if (setMode(kAddrIc9700, CivMode::Usb, kFilterFil2)
            != QByteArray::fromHex("fefea2e0060102fd")) {
        return fail("setMode byte layout is wrong");
    }
    if (setMode(kAddrIc705, CivMode::Cw)
            != QByteArray::fromHex("fefea4e0060301fd")) {
        return fail("setMode default filter (FIL1) byte layout is wrong");
    }
    if (readMode(kAddrIc7610)
            != QByteArray::fromHex("fefe98e004fd")) {
        return fail("readMode byte layout is wrong");
    }

    // --- Response parsers. -----------------------------------------------
    // Frequency read reply from an IC-7300: FE FE E0 94 03 <5 BCD> FD.
    // Payload 00 40 07 14 00 = 14.074 MHz (see BCD vectors above).
    const auto freqReply =
        parseFrame(QByteArray::fromHex("fefee094030040071400fd"));
    if (!freqReply) {
        return fail("frequency reply frame did not parse");
    }
    const auto freqValue = parseFrequencyResponse(*freqReply);
    if (!freqValue || *freqValue != 14074000ull) {
        return fail("parseFrequencyResponse decoded the wrong frequency");
    }
    // Zero Hz is a legal frequency and must not be conflated with an error.
    const auto zeroReply =
        parseFrame(QByteArray::fromHex("fefee094030000000000fd"));
    if (!zeroReply) {
        return fail("zero-frequency reply frame did not parse");
    }
    const auto zeroValue = parseFrequencyResponse(*zeroReply);
    if (!zeroValue || *zeroValue != 0ull) {
        return fail("parseFrequencyResponse should accept 0 Hz");
    }
    // A frequency parser must reject a non-frequency command.
    CivFrame wrongCmd;
    wrongCmd.cmd = kCmdReadMode;
    wrongCmd.payload = QByteArray::fromHex("0040071400");
    if (parseFrequencyResponse(wrongCmd)) {
        return fail("parseFrequencyResponse should reject non-freq command");
    }
    // A frequency parser must reject non-BCD payload nibbles.
    CivFrame badBcd;
    badBcd.cmd = kCmdReadFrequency;
    badBcd.payload = QByteArray::fromHex("00ab071400");
    if (parseFrequencyResponse(badBcd)) {
        return fail("parseFrequencyResponse should reject non-BCD payload");
    }
    // Empty payload has no frequency.
    CivFrame emptyFreq;
    emptyFreq.cmd = kCmdReadFrequency;
    if (parseFrequencyResponse(emptyFreq)) {
        return fail("parseFrequencyResponse should reject empty payload");
    }

    // Mode read reply: FE FE E0 A2 04 <mode> <filter> FD (USB, FIL2).
    const auto modeReply =
        parseFrame(QByteArray::fromHex("fefee0a2040102fd"));
    if (!modeReply) {
        return fail("mode reply frame did not parse");
    }
    const auto modeValue = parseModeResponse(*modeReply);
    if (!modeValue
        || modeValue->mode != CivMode::Usb
        || !modeValue->hasFilter
        || modeValue->filter != kFilterFil2) {
        return fail("parseModeResponse decoded the wrong mode/filter");
    }
    // Mode reply without a filter byte: FE FE E0 A2 04 <mode> FD (CW).
    const auto modeReplyNoFilter =
        parseFrame(QByteArray::fromHex("fefee0a20403fd"));
    if (!modeReplyNoFilter) {
        return fail("filter-less mode reply frame did not parse");
    }
    const auto modeNoFilter = parseModeResponse(*modeReplyNoFilter);
    if (!modeNoFilter
        || modeNoFilter->mode != CivMode::Cw
        || modeNoFilter->hasFilter
        || modeNoFilter->filter != 0) {
        return fail("parseModeResponse should tolerate missing filter byte");
    }
    // A mode parser must reject a non-mode command.
    CivFrame wrongModeCmd;
    wrongModeCmd.cmd = kCmdReadFrequency;
    wrongModeCmd.payload = QByteArray::fromHex("0102");
    if (parseModeResponse(wrongModeCmd)) {
        return fail("parseModeResponse should reject non-mode command");
    }
    // Empty payload has no mode.
    CivFrame emptyMode;
    emptyMode.cmd = kCmdReadMode;
    if (parseModeResponse(emptyMode)) {
        return fail("parseModeResponse should reject empty payload");
    }

    // Scope waveform command constant is exposed but not parsed here.
    if (kCmdScopeWaveform != 0x27) {
        return fail("scope waveform command constant is wrong");
    }

    return 0;
}
