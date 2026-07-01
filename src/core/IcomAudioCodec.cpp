// IcomAudioCodec — audio codec core for Icom network audio (issue #7).
//
// Clean-room design. See docs/icom-cleanroom-design.md. Implements ONLY public
// standards: ITU-T G.711 µ-law companding and linear signed 16-bit PCM. No GPL
// wfview (or other licensed) code, constants, or structure is used here.
//
// UNSUPPORTED-BY-HARDWARE NOTE: Opus is NOT supported by any Icom radio and must
// never be assumed toward hardware. This codec handles only µ-law (G.711) and
// 16-bit signed linear PCM, mono. Sample rate / resampling is handled elsewhere.

#include "IcomAudioCodec.h"

#include <algorithm>
#include <cstring>

namespace AetherSDR::IcomAudio {
namespace {

// Number of bytes in one float32 sample of the AudioEngine representation.
constexpr int kFloatBytes = static_cast<int>(sizeof(float));

// Number of bytes in one PCM16 sample.
constexpr int kPcm16Bytes = 2;

// Read one PCM16 sample from a two-byte little/big-endian pair.
qint16 readPcm16(const uchar* p)
{
    quint16 raw = 0;
    if (kPcm16BigEndian) {
        raw = static_cast<quint16>((static_cast<quint16>(p[0]) << 8)
                                   | static_cast<quint16>(p[1]));
    } else {
        raw = static_cast<quint16>(static_cast<quint16>(p[0])
                                   | (static_cast<quint16>(p[1]) << 8));
    }
    return static_cast<qint16>(raw);
}

// Write one PCM16 sample to a two-byte destination in the configured order.
void writePcm16(char* dst, qint16 sample)
{
    const quint16 raw = static_cast<quint16>(sample);
    if (kPcm16BigEndian) {
        dst[0] = static_cast<char>((raw >> 8) & 0xffu);
        dst[1] = static_cast<char>(raw & 0xffu);
    } else {
        dst[0] = static_cast<char>(raw & 0xffu);
        dst[1] = static_cast<char>((raw >> 8) & 0xffu);
    }
}

// Append a float sample to a byte buffer using the host's native float32 layout
// (matches how AudioEngine's float PCM buffers are produced and consumed).
void appendFloat(QByteArray* out, float value)
{
    char bytes[kFloatBytes];
    std::memcpy(bytes, &value, sizeof(bytes));
    out->append(bytes, kFloatBytes);
}

// Read a native-layout float32 sample from a byte pointer.
float readFloat(const char* p)
{
    float value = 0.0f;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

} // namespace

qint16 muLawDecodeSample(quint8 uVal)
{
    // Standard ITU-T G.711 µ-law expansion. The stored byte is the one's
    // complement of the encoded value, so complement it first.
    const int u = (~static_cast<int>(uVal)) & 0xff;
    const int sign = u & 0x80;
    const int exponent = (u >> 4) & 0x07;
    const int mantissa = u & 0x0f;

    // Reconstruct the magnitude: put the mantissa in place, add the implicit
    // leading bit and the mid-riser half-step, shift by the exponent, then
    // remove the encoder bias.
    int magnitude = ((mantissa << 3) + kMuLawBias) << exponent;
    magnitude -= kMuLawBias;

    const int sample = sign != 0 ? -magnitude : magnitude;
    return static_cast<qint16>(sample);
}

quint8 muLawEncodeSample(qint16 pcm)
{
    // Standard ITU-T G.711 µ-law compression.
    int sample = static_cast<int>(pcm);

    // Capture sign as the 0x80 bit and work with the magnitude.
    int sign = 0;
    if (sample < 0) {
        sign = 0x80;
        sample = -sample;
    }

    // Clip to the maximum representable magnitude, then apply the bias.
    if (sample > kMuLawClip) {
        sample = kMuLawClip;
    }
    sample += kMuLawBias;

    // Find the exponent (segment): position of the most significant set bit
    // above bit 6, searched from bit 12 downward.
    int exponent = 7;
    for (int mask = 0x4000; (sample & mask) == 0 && exponent > 0; mask >>= 1) {
        --exponent;
    }

    // Extract the 4-bit mantissa for that segment.
    const int mantissa = (sample >> (exponent + 3)) & 0x0f;

    // Assemble and store the one's complement of the encoded byte.
    const int uVal = sign | (exponent << 4) | mantissa;
    return static_cast<quint8>((~uVal) & 0xff);
}

float pcm16ToFloat(qint16 sample)
{
    // Divide by 32768 so the full negative range maps to exactly -1.0, then
    // clamp defensively to keep the result inside [-1, 1].
    return std::clamp(static_cast<float>(sample) / 32768.0f, -1.0f, 1.0f);
}

qint16 floatToPcm16(float value)
{
    // Clamp before scaling so out-of-range floats saturate instead of wrapping.
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    // Scale by 32767 so +1.0 maps to the positive full-scale value and -1.0
    // maps to -32767 (symmetric), then round to nearest.
    const float scaled = clamped * 32767.0f;
    const int rounded = static_cast<int>(scaled >= 0.0f ? scaled + 0.5f
                                                        : scaled - 0.5f);
    const int limited = std::clamp(rounded, -32768, 32767);
    return static_cast<qint16>(limited);
}

QByteArray decodeToFloatMono(const QByteArray& payload, IcomAudioFormat fmt)
{
    QByteArray out;
    if (payload.isEmpty()) {
        return out;
    }

    const auto* data = reinterpret_cast<const uchar*>(payload.constData());
    const int size = static_cast<int>(payload.size());

    if (fmt == IcomAudioFormat::ULaw8) {
        out.reserve(size * kFloatBytes);
        for (int i = 0; i < size; ++i) {
            appendFloat(&out, pcm16ToFloat(muLawDecodeSample(data[i])));
        }
        return out;
    }

    // PCM16: only decode whole samples; a trailing odd byte is ignored so we
    // never read past the buffer.
    const int sampleCount = size / kPcm16Bytes;
    out.reserve(sampleCount * kFloatBytes);
    for (int i = 0; i < sampleCount; ++i) {
        const qint16 sample = readPcm16(data + i * kPcm16Bytes);
        appendFloat(&out, pcm16ToFloat(sample));
    }
    return out;
}

QByteArray encodeFromFloatMono(const QByteArray& floatMono, IcomAudioFormat fmt)
{
    QByteArray out;
    if (floatMono.isEmpty()) {
        return out;
    }

    // Only encode whole float32 samples; drop any trailing partial sample.
    const int sampleCount = static_cast<int>(floatMono.size()) / kFloatBytes;
    if (sampleCount <= 0) {
        return out;
    }
    const char* data = floatMono.constData();

    if (fmt == IcomAudioFormat::ULaw8) {
        out.reserve(sampleCount);
        for (int i = 0; i < sampleCount; ++i) {
            const float value = readFloat(data + i * kFloatBytes);
            out.append(static_cast<char>(
                muLawEncodeSample(floatToPcm16(value))));
        }
        return out;
    }

    // PCM16 output in the configured byte order.
    out.resize(sampleCount * kPcm16Bytes);
    char* dst = out.data();
    for (int i = 0; i < sampleCount; ++i) {
        const float value = readFloat(data + i * kFloatBytes);
        writePcm16(dst + i * kPcm16Bytes, floatToPcm16(value));
    }
    return out;
}

} // namespace AetherSDR::IcomAudio
