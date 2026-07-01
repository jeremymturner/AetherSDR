#pragma once

// Icom CI-V command + codec layer (public protocol only).
//
// Derived from Icom's public CI-V Reference Guide; no GPL wfview code
// referenced. See docs/icom-cleanroom-design.md and issue #4.
//
// This is a pure, stateless codec in the spirit of KiwiSdrProtocol: free
// functions and small POD structs, no QObject, no ownership, no I/O. All
// framing/BCD logic is derived purely from the public CI-V frame spec:
//   0xFE 0xFE <toAddr> <fromAddr> <cmd> [subCmd] [data...] 0xFD
// Data fields are BCD unless noted. CI-V has no byte escaping/stuffing, so
// framing is a straight assemble/disassemble around the fixed preamble and
// terminator.

#include <QByteArray>

#include <QtGlobal>

#include <optional>

namespace AetherSDR::IcomCiv {

// Fixed framing bytes (public CI-V spec).
inline constexpr quint8 kPreambleByte = 0xFE;
inline constexpr quint8 kTerminatorByte = 0xFD;

// The controller (PC) address is conventionally 0xE0.
inline constexpr quint8 kControllerAddress = 0xE0;

// A radio may also answer 0x00 ("broadcast"/transceive) as the fromAddr.
inline constexpr quint8 kBroadcastAddress = 0x00;

// Per-radio default CI-V addresses. These are the factory defaults only;
// every one of them is user-changeable in the radio's menu, so callers must
// treat them as defaults, not invariants.
inline constexpr quint8 kAddrIc705 = 0xA4;
inline constexpr quint8 kAddrIc9700 = 0xA2;
inline constexpr quint8 kAddrIc7610 = 0x98;
inline constexpr quint8 kAddrIc7300 = 0x94;   // IC-7300 / IC-7300 MK2

// RX-control v1 command set (public CI-V spec).
inline constexpr quint8 kCmdReadFrequency = 0x03;   // read operating frequency
inline constexpr quint8 kCmdReadMode = 0x04;        // read operating mode
inline constexpr quint8 kCmdSetFrequency = 0x05;    // set operating frequency
inline constexpr quint8 kCmdSetMode = 0x06;         // set operating mode

// Scope waveform command. Parsing of 0x27 waveform payloads lives in a
// separate module (a different issue); only the constant is exposed here.
inline constexpr quint8 kCmdScopeWaveform = 0x27;

// Default operating-frequency width: 5 bytes = 10 BCD digits = 1 Hz res.
inline constexpr int kFrequencyBcdBytes = 5;

// CI-V operating modes (cmd 0x06 mode byte, public spec).
enum class CivMode : quint8 {
    Lsb = 0x00,
    Usb = 0x01,
    Am = 0x02,
    Cw = 0x03,
    Rtty = 0x04,
    Fm = 0x05,
    Wfm = 0x06,
    CwR = 0x07,
    RttyR = 0x08,
    Dv = 0x17,
};

// Filter selector accompanying a mode (cmd 0x06 optional filter byte).
inline constexpr quint8 kFilterFil1 = 0x01;
inline constexpr quint8 kFilterFil2 = 0x02;
inline constexpr quint8 kFilterFil3 = 0x03;

// A decoded CI-V frame.
//
// Payload split: the frame on the wire is
//   FE FE <toAddr> <fromAddr> <cmd> [subCmd] [data...] FD
// `cmd` is stored as its own field. `payload` is everything strictly between
// the cmd byte and the FD terminator, i.e. the (optional) sub-command byte
// followed by any data bytes. An empty payload is legal (e.g. bare reads).
struct CivFrame {
    quint8 toAddr{0};
    quint8 fromAddr{0};
    quint8 cmd{0};
    QByteArray payload;
};

// A decoded mode response (cmd 0x04/0x06 read reply).
struct CivModeResponse {
    CivMode mode{CivMode::Lsb};
    int filter{0};        // 0 when the radio omitted the filter byte
    bool hasFilter{false};
};

// Assemble a raw CI-V frame: prepends FE FE, appends FD. CI-V has no byte
// stuffing/escaping, so this is a straight concatenation of the fixed
// preamble, addresses, command, payload, and terminator.
QByteArray encodeFrame(quint8 toAddr, quint8 fromAddr, quint8 cmd,
                       const QByteArray& payload);

// Parse a single raw CI-V frame. Validates the leading FE FE, the trailing
// FD, and the minimum length (FE FE to from cmd FD = 6 bytes). Returns
// nullopt on any malformed input. Never indexes past the buffer.
std::optional<CivFrame> parseFrame(const QByteArray& raw);

// Frequency <-> little-endian packed BCD.
//
// Icom sends frequency as little-endian BCD: each byte holds two decimal
// digits (high nibble = tens, low nibble = units), least-significant pair
// first. 14074000 Hz over 5 bytes -> 00 40 07 14 00.
QByteArray freqToBcd(quint64 hz, int numBytes = kFrequencyBcdBytes);
quint64 bcdToFreq(const QByteArray& bcd);

// Command builders. Each returns a ready-to-send raw frame via encodeFrame,
// addressed from the controller (kControllerAddress) to the given radio.
QByteArray setFrequency(quint8 toAddr, quint64 hz);
QByteArray readFrequency(quint8 toAddr);
QByteArray setMode(quint8 toAddr, CivMode mode, quint8 filter = kFilterFil1);
QByteArray readMode(quint8 toAddr);

// Response parsers. These accept a frame already validated by parseFrame and
// carrying the corresponding command (0x03/0x05 for frequency, 0x04/0x06 for
// mode). Return nullopt when the payload does not match the expected shape.
std::optional<quint64> parseFrequencyResponse(const CivFrame& frame);
std::optional<CivModeResponse> parseModeResponse(const CivFrame& frame);

} // namespace AetherSDR::IcomCiv
