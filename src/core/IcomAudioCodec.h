#pragma once

// IcomAudioCodec — audio codec core for Icom network audio (issue #7).
//
// Clean-room design. See docs/icom-cleanroom-design.md for the derivation and
// the interoperability record. This module implements ONLY public, published
// standards: ITU-T G.711 µ-law companding and linear signed 16-bit PCM. It
// contains no code, structure, or copied constants derived from GPL wfview or
// any other GPL/licensed source; the companding and framing here are written
// from the public ITU-T G.711 recommendation and the plain PCM definition.
//
// UNSUPPORTED-BY-HARDWARE NOTE: Opus is NOT supported by any Icom radio and
// must never be assumed toward hardware. Real Icom network radios negotiate
// only 8-bit µ-law (G.711) and 16-bit signed linear PCM (mono). This codec is
// deliberately limited to those two formats. The sample rate is negotiated
// elsewhere (typical values 8000/16000/32000/48000 Hz); this codec is
// rate-agnostic and performs no resampling.
//
// Icom network audio is carried on UDP port 50003.

#include <QByteArray>

#include <QtGlobal>

namespace AetherSDR::IcomAudio {

// -----------------------------------------------------------------------------
// Format description
// -----------------------------------------------------------------------------

// The two on-the-wire sample formats an Icom radio actually negotiates.
enum class IcomAudioFormat {
    PCM16,   // 16-bit signed linear PCM, mono.
    ULaw8,   // 8-bit G.711 µ-law, mono.
};

// A negotiated audio format. sampleRateHz and channels are informational for
// callers; the codec functions below operate on mono streams and do not use
// the rate (resampling is handled elsewhere).
struct AudioFormatDesc {
    IcomAudioFormat format{IcomAudioFormat::PCM16};
    int sampleRateHz{0};
    int channels{1};
};

// -----------------------------------------------------------------------------
// Byte-order / format assumptions (VERIFY AT CAPTURE TIME)
// -----------------------------------------------------------------------------

// ASSUMPTION — PCM16 network byte order. Icom PCM16 network audio samples are
// assumed to arrive in BIG-ENDIAN (network) byte order. This has NOT yet been
// confirmed against a captured stream from real hardware. It is exposed as a
// single compile-time switch so the parent can flip it after capture-time
// verification without touching the companding or framing logic below.
inline constexpr bool kPcm16BigEndian = true;

// -----------------------------------------------------------------------------
// G.711 µ-law constants (public ITU-T standard values)
// -----------------------------------------------------------------------------

inline constexpr int kMuLawBias = 0x84;    // 132; added before segment search.
inline constexpr int kMuLawClip = 32635;   // Magnitude clip = 32767 - kMuLawBias.

// -----------------------------------------------------------------------------
// G.711 µ-law per-sample codec
// -----------------------------------------------------------------------------

// Decode one µ-law byte to a signed 16-bit linear PCM sample. Standard G.711
// expansion (complement input, split sign/exponent/mantissa, reconstruct).
qint16 muLawDecodeSample(quint8 uVal);

// Encode one signed 16-bit linear PCM sample to a µ-law byte. Standard G.711
// compression with BIAS/CLIP and complemented output byte.
quint8 muLawEncodeSample(qint16 pcm);

// -----------------------------------------------------------------------------
// Linear PCM16 <-> float helpers
// -----------------------------------------------------------------------------

// Convert a signed 16-bit PCM sample to float in the range [-1, 1].
float pcm16ToFloat(qint16 sample);

// Convert a float sample to signed 16-bit PCM with clamping to [-1, 1] before
// scaling, so out-of-range floats saturate rather than wrap.
qint16 floatToPcm16(float value);

// -----------------------------------------------------------------------------
// Frame conversion to/from the AudioEngine float representation
// -----------------------------------------------------------------------------

// Decode a raw network audio payload of the given format into interleaved
// float32 mono samples (the representation the AudioEngine consumes), each in
// the range [-1, 1].
//
// Boundary-checked: an empty payload yields an empty result; for PCM16 an odd
// trailing byte (not a whole sample) is dropped rather than read past. Never
// reads past the buffer. PCM16 endianness follows kPcm16BigEndian.
QByteArray decodeToFloatMono(const QByteArray& payload, IcomAudioFormat fmt);

// Encode interleaved float32 mono samples into a raw network audio payload of
// the given format (future TX path). A float input whose length is not a whole
// number of float32 samples has its trailing partial sample dropped. PCM16
// output endianness follows kPcm16BigEndian.
QByteArray encodeFromFloatMono(const QByteArray& floatMono, IcomAudioFormat fmt);

} // namespace AetherSDR::IcomAudio
