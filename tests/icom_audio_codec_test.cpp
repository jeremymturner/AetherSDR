// Freestanding test for AetherSDR::IcomAudio (issue #7 audio codec core).
//
// No test framework: a plain int main() that returns 0 on success and 1 on the
// first failure, printing a diagnostic to stderr. Deterministic and
// self-contained; matches the style of tests/kiwi_sdr_protocol_test.cpp.

#include "core/IcomAudioCodec.h"

#include <QByteArray>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

bool nearlyEqual(float a, float b, float epsilon = 1.0e-4f)
{
    return std::fabs(a - b) <= epsilon;
}

int fail(const char* message)
{
    std::fprintf(stderr, "icom_audio_codec_test: %s\n", message);
    return 1;
}

float floatAt(const QByteArray& bytes, int sampleIndex)
{
    float value = 0.0f;
    std::memcpy(&value,
                bytes.constData() + sampleIndex * static_cast<int>(sizeof(float)),
                sizeof(value));
    return value;
}

QByteArray floatMonoFrom(const float* samples, int count)
{
    QByteArray out;
    out.reserve(count * static_cast<int>(sizeof(float)));
    for (int i = 0; i < count; ++i) {
        char bytes[sizeof(float)];
        std::memcpy(bytes, &samples[i], sizeof(bytes));
        out.append(bytes, static_cast<int>(sizeof(bytes)));
    }
    return out;
}

} // namespace

int main()
{
    using namespace AetherSDR::IcomAudio;

    // --- G.711 µ-law reference decode vectors (table-driven) ----------------
    // Known ITU-T G.711 reference pairs. 0xFF is µ-law "positive zero" and
    // decodes to 0; 0x00 decodes to the largest-magnitude negative value.
    struct DecodeVector {
        quint8 code;
        qint16 expected;
    };
    const DecodeVector decodeVectors[] = {
        {0xFF, 0},        // positive zero
        {0x7F, 0},        // negative zero (also decodes to 0)
        {0x00, -32124},   // largest negative magnitude
        {0x80, 32124},    // largest positive magnitude
        {0xF0, 120},      // small positive mid-range
        {0x2A, -5372},    // mid-range negative
    };
    for (const auto& v : decodeVectors) {
        if (muLawDecodeSample(v.code) != v.expected) {
            return fail("mu-law decode reference vector mismatch");
        }
    }

    // --- G.711 µ-law reference encode vectors -------------------------------
    if (muLawEncodeSample(0) != 0xFF) {
        return fail("mu-law encode of 0 should be 0xFF");
    }
    if (muLawEncodeSample(32767) != 0x80) {
        return fail("mu-law encode of +full-scale should be 0x80");
    }
    if (muLawEncodeSample(-32768) != 0x00) {
        return fail("mu-law encode of -full-scale should be 0x00");
    }

    // --- µ-law idempotence over the full byte range -------------------------
    // encode(decode(x)) == x for every µ-law byte EXCEPT 0x7F: both 0x7F and
    // 0xFF decode to 0, and 0 re-encodes to 0xFF, so 0x7F is the single
    // standard non-idempotent code. Assert that precisely.
    for (int x = 0; x <= 255; ++x) {
        const quint8 code = static_cast<quint8>(x);
        const quint8 round = muLawEncodeSample(muLawDecodeSample(code));
        if (code == 0x7F) {
            if (round != 0xFF) {
                return fail("mu-law 0x7F should re-encode to 0xFF");
            }
        } else if (round != code) {
            return fail("mu-law encode(decode(x)) idempotence failed");
        }
    }

    // --- PCM16 <-> float helpers --------------------------------------------
    if (!nearlyEqual(pcm16ToFloat(0), 0.0f)
        || !nearlyEqual(pcm16ToFloat(32767), 32767.0f / 32768.0f)
        || !nearlyEqual(pcm16ToFloat(-32768), -1.0f)) {
        return fail("pcm16ToFloat mapping is wrong");
    }
    if (floatToPcm16(0.0f) != 0
        || floatToPcm16(1.0f) != 32767
        || floatToPcm16(-1.0f) != -32767) {
        return fail("floatToPcm16 mapping is wrong");
    }
    // Clamping: out-of-range floats must saturate, not wrap.
    if (floatToPcm16(2.0f) != 32767 || floatToPcm16(-2.0f) != -32767) {
        return fail("floatToPcm16 must clamp out-of-range input");
    }

    // --- PCM16 -> float -> PCM16 round-trip within tolerance ----------------
    for (int s = -32768; s <= 32767; s += 257) {
        const qint16 sample = static_cast<qint16>(s);
        const qint16 back = floatToPcm16(pcm16ToFloat(sample));
        // Allow +/-1 LSB: pcm16ToFloat uses /32768 and floatToPcm16 uses
        // *32767, an intentional asymmetry that keeps -1.0 exact.
        if (std::abs(static_cast<int>(back) - static_cast<int>(sample)) > 1) {
            return fail("PCM16<->float round-trip exceeded 1 LSB tolerance");
        }
    }

    // --- PCM16 decode endianness with a documented sample -------------------
    // Bytes 0x12 0x34. Big-endian => 0x1234 = 4660; little-endian => 0x3412.
    // kPcm16BigEndian selects which the codec produces; assert the active one.
    {
        const QByteArray payload = QByteArray::fromHex("1234");
        const QByteArray decoded = decodeToFloatMono(payload,
                                                     IcomAudioFormat::PCM16);
        if (decoded.size() != static_cast<int>(sizeof(float))) {
            return fail("PCM16 decode should yield exactly one float sample");
        }
        const qint16 expectedSample =
            kPcm16BigEndian ? static_cast<qint16>(0x1234)
                            : static_cast<qint16>(0x3412);
        if (!nearlyEqual(floatAt(decoded, 0), pcm16ToFloat(expectedSample))) {
            return fail("PCM16 decode endianness does not match kPcm16BigEndian");
        }
    }

    // --- PCM16 encode/decode framing round-trip -----------------------------
    {
        const float samples[] = {0.0f, 0.5f, -0.5f, 0.999f, -1.0f};
        const int count = static_cast<int>(sizeof(samples) / sizeof(samples[0]));
        const QByteArray floatMono = floatMonoFrom(samples, count);
        const QByteArray encoded =
            encodeFromFloatMono(floatMono, IcomAudioFormat::PCM16);
        if (encoded.size() != count * 2) {
            return fail("PCM16 encode produced wrong byte count");
        }
        const QByteArray decoded =
            decodeToFloatMono(encoded, IcomAudioFormat::PCM16);
        if (decoded.size() != count * static_cast<int>(sizeof(float))) {
            return fail("PCM16 encode->decode produced wrong sample count");
        }
        for (int i = 0; i < count; ++i) {
            if (!nearlyEqual(floatAt(decoded, i), samples[i], 1.0e-3f)) {
                return fail("PCM16 float framing round-trip drifted");
            }
        }
    }

    // --- µ-law decode framing -----------------------------------------------
    {
        const QByteArray payload = QByteArray::fromHex("FF002AF0");
        const QByteArray decoded =
            decodeToFloatMono(payload, IcomAudioFormat::ULaw8);
        if (decoded.size() != 4 * static_cast<int>(sizeof(float))) {
            return fail("mu-law decode produced wrong sample count");
        }
        if (!nearlyEqual(floatAt(decoded, 0), pcm16ToFloat(0))
            || !nearlyEqual(floatAt(decoded, 1), pcm16ToFloat(-32124))
            || !nearlyEqual(floatAt(decoded, 2), pcm16ToFloat(-5372))
            || !nearlyEqual(floatAt(decoded, 3), pcm16ToFloat(120))) {
            return fail("mu-law decode framing values are wrong");
        }
    }

    // --- µ-law encode framing (float mono -> µ-law bytes) -------------------
    {
        const float samples[] = {0.0f, 1.0f, -1.0f};
        const int count = static_cast<int>(sizeof(samples) / sizeof(samples[0]));
        const QByteArray floatMono = floatMonoFrom(samples, count);
        const QByteArray encoded =
            encodeFromFloatMono(floatMono, IcomAudioFormat::ULaw8);
        if (encoded.size() != count) {
            return fail("mu-law encode produced wrong byte count");
        }
        if (static_cast<quint8>(encoded[0]) != muLawEncodeSample(floatToPcm16(0.0f))
            || static_cast<quint8>(encoded[1]) != muLawEncodeSample(32767)
            || static_cast<quint8>(encoded[2]) != muLawEncodeSample(-32767)) {
            return fail("mu-law encode framing values are wrong");
        }
    }

    // --- Boundary / safety: empty and odd-length input ----------------------
    if (!decodeToFloatMono(QByteArray(), IcomAudioFormat::PCM16).isEmpty()
        || !decodeToFloatMono(QByteArray(), IcomAudioFormat::ULaw8).isEmpty()) {
        return fail("empty payload decode must yield empty output");
    }
    if (!encodeFromFloatMono(QByteArray(), IcomAudioFormat::PCM16).isEmpty()
        || !encodeFromFloatMono(QByteArray(), IcomAudioFormat::ULaw8).isEmpty()) {
        return fail("empty float payload encode must yield empty output");
    }
    {
        // Odd PCM16 payload: the trailing byte is not a whole sample and must
        // be dropped, never read past. 5 bytes => 2 whole samples decoded.
        const QByteArray odd = QByteArray::fromHex("00010203FF");
        const QByteArray decoded = decodeToFloatMono(odd, IcomAudioFormat::PCM16);
        if (decoded.size() != 2 * static_cast<int>(sizeof(float))) {
            return fail("odd-length PCM16 payload must drop the trailing byte");
        }
    }
    {
        // Float input shorter than one whole float32 sample must yield empty.
        const QByteArray partial = QByteArray::fromHex("0011");
        if (!encodeFromFloatMono(partial, IcomAudioFormat::PCM16).isEmpty()
            || !encodeFromFloatMono(partial, IcomAudioFormat::ULaw8).isEmpty()) {
            return fail("sub-sample float input must yield empty encode output");
        }
    }

    // --- AudioFormatDesc defaults -------------------------------------------
    {
        const AudioFormatDesc desc{IcomAudioFormat::ULaw8, 16000, 1};
        if (desc.format != IcomAudioFormat::ULaw8
            || desc.sampleRateHz != 16000
            || desc.channels != 1) {
            return fail("AudioFormatDesc field wiring is wrong");
        }
    }

    return 0;
}
