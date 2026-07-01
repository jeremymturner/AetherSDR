// Icom CI-V command + codec layer (public protocol only).
//
// Derived from Icom's public CI-V Reference Guide; no GPL wfview code
// referenced. See docs/icom-cleanroom-design.md and issue #4.

#include "IcomCivCodec.h"

#include <algorithm>

namespace AetherSDR::IcomCiv {
namespace {

// Minimum on-wire frame: FE FE <to> <from> <cmd> FD.
constexpr int kMinFrameBytes = 6;

// Maximum BCD width we will pack/unpack. A 5-byte operating frequency is the
// documented width; cap generously to guard against nonsense input without
// imposing a tight coupling to the frequency case.
constexpr int kMaxBcdBytes = 8;

quint8 toBcdByte(int tens, int units)
{
    return static_cast<quint8>(((tens & 0x0f) << 4) | (units & 0x0f));
}

} // namespace

QByteArray encodeFrame(quint8 toAddr, quint8 fromAddr, quint8 cmd,
                       const QByteArray& payload)
{
    QByteArray frame;
    frame.reserve(kMinFrameBytes + static_cast<int>(payload.size()));
    frame.append(static_cast<char>(kPreambleByte));
    frame.append(static_cast<char>(kPreambleByte));
    frame.append(static_cast<char>(toAddr));
    frame.append(static_cast<char>(fromAddr));
    frame.append(static_cast<char>(cmd));
    frame.append(payload);
    frame.append(static_cast<char>(kTerminatorByte));
    return frame;
}

std::optional<CivFrame> parseFrame(const QByteArray& raw)
{
    // Boundary validation is a hard requirement: never index past the buffer.
    if (raw.size() < kMinFrameBytes) {
        return std::nullopt;
    }

    const auto* bytes = reinterpret_cast<const uchar*>(raw.constData());
    const int size = static_cast<int>(raw.size());
    if (bytes[0] != kPreambleByte || bytes[1] != kPreambleByte) {
        return std::nullopt;
    }
    if (bytes[size - 1] != kTerminatorByte) {
        return std::nullopt;
    }
    // A stray FE FE ... FE FE payload or an internal terminator is not our
    // concern here: we decode exactly one well-formed frame. Reject frames
    // whose fixed header bytes are themselves the terminator, which would
    // imply a truncated frame that merely happens to end in FD.
    if (bytes[2] == kTerminatorByte || bytes[3] == kTerminatorByte) {
        return std::nullopt;
    }

    CivFrame frame;
    frame.toAddr = bytes[2];
    frame.fromAddr = bytes[3];
    frame.cmd = bytes[4];
    // payload = everything between cmd (index 4) and the FD terminator
    // (index size - 1). May be empty (e.g. a bare read request/echo).
    const int payloadBytes = size - kMinFrameBytes;
    if (payloadBytes > 0) {
        frame.payload = raw.mid(5, payloadBytes);
    }
    return frame;
}

QByteArray freqToBcd(quint64 hz, int numBytes)
{
    const int width = std::clamp(numBytes, 0, kMaxBcdBytes);
    QByteArray bcd(width, '\0');
    // Little-endian: least-significant decimal pair goes in byte 0.
    for (int i = 0; i < width; ++i) {
        const int units = static_cast<int>(hz % 10);
        hz /= 10;
        const int tens = static_cast<int>(hz % 10);
        hz /= 10;
        bcd[i] = static_cast<char>(toBcdByte(tens, units));
    }
    return bcd;
}

quint64 bcdToFreq(const QByteArray& bcd)
{
    quint64 hz = 0;
    // Walk most-significant byte first, folding each decimal pair in.
    for (int i = static_cast<int>(bcd.size()) - 1; i >= 0; --i) {
        const auto byte = static_cast<quint8>(bcd[i]);
        const int tens = (byte >> 4) & 0x0f;
        const int units = byte & 0x0f;
        // Guard against non-BCD nibbles (>= 10) so garbage bytes do not
        // silently produce a plausible-looking frequency.
        if (tens > 9 || units > 9) {
            return 0;
        }
        hz = (hz * 100) + static_cast<quint64>((tens * 10) + units);
    }
    return hz;
}

QByteArray setFrequency(quint8 toAddr, quint64 hz)
{
    return encodeFrame(toAddr, kControllerAddress, kCmdSetFrequency,
                       freqToBcd(hz, kFrequencyBcdBytes));
}

QByteArray readFrequency(quint8 toAddr)
{
    return encodeFrame(toAddr, kControllerAddress, kCmdReadFrequency,
                       QByteArray());
}

QByteArray setMode(quint8 toAddr, CivMode mode, quint8 filter)
{
    QByteArray payload;
    payload.reserve(2);
    payload.append(static_cast<char>(static_cast<quint8>(mode)));
    payload.append(static_cast<char>(filter));
    return encodeFrame(toAddr, kControllerAddress, kCmdSetMode, payload);
}

QByteArray readMode(quint8 toAddr)
{
    return encodeFrame(toAddr, kControllerAddress, kCmdReadMode, QByteArray());
}

std::optional<quint64> parseFrequencyResponse(const CivFrame& frame)
{
    if (frame.cmd != kCmdReadFrequency && frame.cmd != kCmdSetFrequency) {
        return std::nullopt;
    }
    if (frame.payload.isEmpty()
        || frame.payload.size() > kMaxBcdBytes) {
        return std::nullopt;
    }
    // Reject non-BCD content: bcdToFreq returns 0 on a bad nibble, but 0 is
    // also a legal frequency, so validate the nibbles explicitly here.
    for (const char raw : frame.payload) {
        const auto byte = static_cast<quint8>(raw);
        if (((byte >> 4) & 0x0f) > 9 || (byte & 0x0f) > 9) {
            return std::nullopt;
        }
    }
    return bcdToFreq(frame.payload);
}

std::optional<CivModeResponse> parseModeResponse(const CivFrame& frame)
{
    if (frame.cmd != kCmdReadMode && frame.cmd != kCmdSetMode) {
        return std::nullopt;
    }
    if (frame.payload.isEmpty()) {
        return std::nullopt;
    }

    CivModeResponse response;
    response.mode = static_cast<CivMode>(static_cast<quint8>(frame.payload[0]));
    if (frame.payload.size() >= 2) {
        response.filter = static_cast<quint8>(frame.payload[1]);
        response.hasFilter = true;
    }
    return response;
}

} // namespace AetherSDR::IcomCiv
