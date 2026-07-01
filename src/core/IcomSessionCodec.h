#pragma once

#include <QByteArray>
#include <QVector>
#include <QtGlobal>

#include <cstdint>
#include <optional>

// ---------------------------------------------------------------------------
// Icom IP-remote session/framing codec (issue #3 / epic #1).
//
// CLEAN-ROOM PROVENANCE (AetherSDR Constitution Principle IV,
// read-and-reimplement):
//   The byte layout modelled below was DERIVED by reading permitted
//   open-source references — principally wfview (gitlab.com/eliggett/wfview,
//   GPLv3) and the public reverse-engineering notes around kappanhang and the
//   ham-radio "Icom Network Protocol" forum thread — and then re-expressed as
//   AetherSDR's OWN code.  Nothing here is a verbatim copy of wfview's structs,
//   constant tables, or functions: the field names, the struct shapes, the
//   encode/parse helpers, the boundary checks and the constant naming are all
//   original.  The proprietary Icom RS-BA1 binary was NEVER consulted.
//
// UNVERIFIED-AGAINST-HARDWARE: every constant, offset and opcode in this file
// is PROVISIONAL until confirmed by a first-party packet capture from a real
// radio (IC-705 / IC-9700 / IC-7610).  Do not treat any magic value as final.
//
// Design: this translation unit is a PURE codec — plain functions + POD
// structs, no QObject, no sockets, no timers, no global state.  That keeps it
// freestanding-testable (see tests/icom_session_codec_test.cpp), mirroring the
// KiwiSdrProtocol pure-codec style.  IcomUdpTransport owns the sockets and the
// state machine and calls into here for every byte it puts on or takes off the
// wire.
// ---------------------------------------------------------------------------

namespace AetherSDR::IcomSession {

// --- Wire geometry ---------------------------------------------------------
// The Icom control/stream packets share a fixed 16-byte little-endian header:
//   offset 0x00  quint32 len      total datagram length (header + body)
//   offset 0x04  quint16 type     opcode (see kOpcode* below)
//   offset 0x06  quint16 seq      per-direction sequence counter
//   offset 0x08  quint32 sentid   the SENDER's session id (our "local id")
//   offset 0x0C  quint32 rcvdid   the RECEIVER's session id (the "remote id")
// All multi-byte header fields are little-endian.  PROVISIONAL.
inline constexpr int kHeaderSize = 16;               // 0x10

// Fixed control-packet sizes (PROVISIONAL — from reading wfview's size defs).
inline constexpr int kControlSize   = 0x10;          // idle / are-you-there / etc.
inline constexpr int kPingSize      = 0x15;          // ping / keepalive-with-time
inline constexpr int kOpenCloseSize = 0x16;          // stream open/close request

// Stream envelope: a control header, then a small stream sub-header, then the
// wrapped CI-V frame or audio payload.  The inner payload starts at 0x15.
inline constexpr int kStreamHeaderSize = 0x15;       // header(0x10) + 5-byte sub

// --- Opcodes (the `type` field) --------------------------------------------
// Values derived from reading wfview's control dispatch (its numeric type
// comparisons) and cross-checked against the public forum notes.  Named here
// by AetherSDR; the mapping is PROVISIONAL until hardware-confirmed.
inline constexpr quint16 kOpcodeIdle          = 0x00; // periodic keepalive / ack carrier
inline constexpr quint16 kOpcodeRetransmit    = 0x01; // resend request (single or range)
inline constexpr quint16 kOpcodeAreYouThere   = 0x03; // session opener (client -> radio)
inline constexpr quint16 kOpcodeIAmHere       = 0x04; // opener reply (radio -> client)
inline constexpr quint16 kOpcodeDisconnect    = 0x05; // clean session teardown
inline constexpr quint16 kOpcodeAreYouReady   = 0x06; // handshake second leg
inline constexpr quint16 kOpcodePing          = 0x07; // ping request/response w/ timestamp

// Stream sub-header markers (PROVISIONAL).
inline constexpr quint8  kStreamReplyMarker   = 0xC1; // byte at 0x10 on data frames

// --- Parsed views ----------------------------------------------------------

// The common header, decoded.  `valid` is false if the buffer was too short.
struct ControlHeader {
    quint32 len{0};
    quint16 type{0};
    quint16 seq{0};
    quint32 sentId{0};   // remote's `sentid` == the radio's session id, from its POV
    quint32 rcvdId{0};   // remote's `rcvdid` == our session id echoed back
    bool valid{false};
};

// Kind of control datagram recognised by parseControl().
enum class ControlKind {
    Unknown,
    Idle,
    Retransmit,      // one or more sequence numbers requested (see retransmitSeqs)
    AreYouThere,
    IAmHere,
    Disconnect,
    AreYouReady,
    Ping,
};

// A fully-parsed control datagram.  Only the fields relevant to `kind` are
// populated; everything is bounds-checked before it is filled.
struct ParsedControl {
    ControlHeader header;
    ControlKind kind{ControlKind::Unknown};

    // Ping (kind == Ping): the reply flag (0 = request, 1 = response) and the
    // 32-bit device-uptime timestamp echoed on ping exchange.
    quint8 pingReply{0};
    quint32 pingTime{0};

    // Retransmit (kind == Retransmit): the sequence number(s) the peer wants
    // resent.  A 0x10-length control carries exactly one in header.seq; a
    // longer datagram carries a list appended after the header.
    QVector<quint16> retransmitSeqs;
};

// A stream datagram (50002 CI-V / 50003 audio) unwrapped down to its payload.
struct ParsedStream {
    ControlHeader header;
    quint16 innerSeq{0};   // the big-endian stream sub-sequence at 0x13
    QByteArray payload;    // the wrapped CI-V frame or audio bytes (from 0x15)
    bool valid{false};
};

// --- Encoders --------------------------------------------------------------
// Each returns a ready-to-send datagram.  `localId` is our session id
// (sent as `sentid`); `remoteId` is the radio's (sent as `rcvdid`), 0 until we
// learn it from the I-am-here reply.

// A bare 16-byte control packet with the given opcode + sequence.  Used for
// idle keepalive, are-you-there, are-you-ready, disconnect, and single-seq
// retransmit requests.
QByteArray encodeControl(quint16 opcode, quint16 seq,
                         quint32 localId, quint32 remoteId);

// Convenience wrappers naming the handshake/keepalive packets.
QByteArray encodeAreYouThere(quint16 seq, quint32 localId);
QByteArray encodeAreYouReady(quint16 seq, quint32 localId, quint32 remoteId);
QByteArray encodeIdle(quint16 seq, quint32 localId, quint32 remoteId);
QByteArray encodeDisconnect(quint16 seq, quint32 localId, quint32 remoteId);

// A ping packet (opcode 0x07): header + reply flag + 32-bit timestamp.
// `reply` = 0 to originate a ping, 1 to answer one; `deviceTimeMs` echoes the
// radio uptime timestamp (ignored / free on an outbound request).
QByteArray encodePing(quint16 seq, quint32 localId, quint32 remoteId,
                      quint8 reply, quint32 deviceTimeMs);

// A retransmit request for a list of missing sequence numbers.  With a single
// entry this emits the compact 0x10-length form (seq in the header); with more
// it emits the extended form (header seq = 0, list appended little-endian).
// An empty list yields an empty QByteArray.
QByteArray encodeRetransmit(const QVector<quint16>& seqs,
                            quint32 localId, quint32 remoteId);

// Wrap a CI-V frame or audio payload in the stream envelope for 50002/50003.
// `streamSeq` is the big-endian inner sequence counter the radio tracks
// separately from the control-port `seq`.
QByteArray encodeStreamPayload(quint16 seq, quint16 streamSeq,
                               quint32 localId, quint32 remoteId,
                               const QByteArray& payload);

// --- Decoders --------------------------------------------------------------

// Decode just the shared 16-byte header.  Returns a header with valid == false
// if `datagram` is shorter than kHeaderSize.  Never indexes past the buffer.
ControlHeader parseHeader(const QByteArray& datagram);

// Parse a control-port datagram (are-you-there / i-am-here / are-you-ready /
// ping / idle / disconnect / retransmit).  Returns std::nullopt when the
// buffer is malformed (too short for its claimed kind).  Boundary-validated.
std::optional<ParsedControl> parseControl(const QByteArray& datagram);

// Unwrap a 50002/50003 stream datagram back to its inner payload.  Returns
// std::nullopt for anything too short to be a stream frame, or whose declared
// inner length overruns the datagram.  Boundary-validated.
std::optional<ParsedStream> parseStreamPayload(const QByteArray& datagram);

} // namespace AetherSDR::IcomSession
