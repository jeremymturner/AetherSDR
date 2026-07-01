#include "IcomSessionCodec.h"

#include <QtEndian>

#include <cstring>

// ---------------------------------------------------------------------------
// See IcomSessionCodec.h for the full clean-room provenance note.  In short:
// the layout was DERIVED from permitted open-source references (wfview et al.)
// and re-implemented here as original AetherSDR code; the RS-BA1 binary was
// never consulted; every constant/offset is PROVISIONAL until confirmed by a
// first-party hardware capture.
//
// Implementation note: rather than reinterpret_cast'ing a packed struct over
// the buffer, we assemble/read every field explicitly with qToLittleEndian /
// qFromLittleEndian at named offsets.  That is intentional — it is portable
// (no packing/alignment assumptions), it is our own expression of the format,
// and it makes each byte offset auditable against a capture.
// ---------------------------------------------------------------------------

namespace AetherSDR::IcomSession {

namespace {

// Header field offsets within the shared 16-byte control header.
constexpr int kOffLen    = 0x00;  // quint32 LE
constexpr int kOffType   = 0x04;  // quint16 LE
constexpr int kOffSeq    = 0x06;  // quint16 LE
constexpr int kOffSentId = 0x08;  // quint32 LE
constexpr int kOffRcvdId = 0x0C;  // quint32 LE

// Ping sub-fields.
constexpr int kOffPingReply = 0x10;  // quint8
constexpr int kOffPingTime  = 0x11;  // quint32 LE

// Stream sub-header fields (data envelope for 50002/50003).
constexpr int kOffStreamReply   = 0x10;  // quint8  (kStreamReplyMarker)
constexpr int kOffStreamDataLen = 0x11;  // quint16 LE  (payload length)
constexpr int kOffStreamSeq     = 0x13;  // quint16 BE  (inner sequence — big-endian!)
constexpr int kOffStreamPayload = 0x15;  // payload begins here

// Little-endian POKE helpers.  `buf` must already be sized to hold the field.
void poke16le(QByteArray& buf, int offset, quint16 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar*>(buf.data()) + offset);
}

void poke32le(QByteArray& buf, int offset, quint32 value)
{
    qToLittleEndian(value, reinterpret_cast<uchar*>(buf.data()) + offset);
}

// Little-endian PEEK helpers.  Callers MUST bounds-check before calling.
quint16 peek16le(const QByteArray& buf, int offset)
{
    return qFromLittleEndian<quint16>(
        reinterpret_cast<const uchar*>(buf.constData()) + offset);
}

quint32 peek32le(const QByteArray& buf, int offset)
{
    return qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(buf.constData()) + offset);
}

quint16 peek16be(const QByteArray& buf, int offset)
{
    return qFromBigEndian<quint16>(
        reinterpret_cast<const uchar*>(buf.constData()) + offset);
}

// Lay down the shared 16-byte header into a zero-initialised buffer of at
// least kHeaderSize bytes.  `len` is the *whole* datagram length.
void writeHeader(QByteArray& buf, quint32 len, quint16 type, quint16 seq,
                 quint32 localId, quint32 remoteId)
{
    poke32le(buf, kOffLen, len);
    poke16le(buf, kOffType, type);
    poke16le(buf, kOffSeq, seq);
    poke32le(buf, kOffSentId, localId);
    poke32le(buf, kOffRcvdId, remoteId);
}

ControlKind kindForOpcode(quint16 type)
{
    switch (type) {
    case kOpcodeIdle:        return ControlKind::Idle;
    case kOpcodeRetransmit:  return ControlKind::Retransmit;
    case kOpcodeAreYouThere: return ControlKind::AreYouThere;
    case kOpcodeIAmHere:     return ControlKind::IAmHere;
    case kOpcodeDisconnect:  return ControlKind::Disconnect;
    case kOpcodeAreYouReady: return ControlKind::AreYouReady;
    case kOpcodePing:        return ControlKind::Ping;
    default:                 return ControlKind::Unknown;
    }
}

// The 95-entry substitution table, indexed by (printable ASCII code 32..126).
// Verbatim from the documented open-source references (kappanhang passcode.go /
// wfview), which include real-radio captures confirming it.
constexpr quint8 kPasscodeTable[95] = {
    0x47, 0x5d, 0x4c, 0x42, 0x66, 0x20, 0x23, 0x46, 0x4e, 0x57, 0x45, 0x3d,
    0x67, 0x76, 0x60, 0x41, 0x62, 0x39, 0x59, 0x2d, 0x68, 0x7e, 0x7c, 0x65,
    0x7d, 0x49, 0x29, 0x72, 0x73, 0x78, 0x21, 0x6e, 0x5a, 0x5e, 0x4a, 0x3e,
    0x71, 0x2c, 0x2a, 0x54, 0x3c, 0x3a, 0x63, 0x4f, 0x43, 0x75, 0x27, 0x79,
    0x5b, 0x35, 0x70, 0x48, 0x6b, 0x56, 0x6f, 0x34, 0x32, 0x6c, 0x30, 0x61,
    0x6d, 0x7b, 0x2f, 0x4b, 0x64, 0x38, 0x2b, 0x2e, 0x50, 0x40, 0x3f, 0x55,
    0x33, 0x37, 0x25, 0x77, 0x24, 0x26, 0x74, 0x6a, 0x28, 0x53, 0x4d, 0x69,
    0x22, 0x5c, 0x44, 0x31, 0x36, 0x58, 0x3b, 0x7a, 0x51, 0x5f, 0x52,
};

} // namespace

QByteArray passcode(const QString& text)
{
    const QByteArray latin = text.toLatin1();
    QByteArray out(16, '\0');
    const int n = qMin(latin.size(), 16);
    for (int i = 0; i < n; ++i) {
        int p = static_cast<quint8>(latin.at(i)) + i;
        if (p > 126) {
            p = 32 + (p % 127);
        }
        // Guard: only printable-range inputs map through the table; anything
        // else stays zero (matches the reference, which only ever feeds ASCII).
        if (p >= 32 && p <= 126) {
            out[i] = static_cast<char>(kPasscodeTable[p - 32]);
        }
    }
    return out;
}

// --- Encoders --------------------------------------------------------------

QByteArray encodeControl(quint16 opcode, quint16 seq,
                         quint32 localId, quint32 remoteId)
{
    QByteArray buf(kControlSize, '\0');
    writeHeader(buf, kControlSize, opcode, seq, localId, remoteId);
    return buf;
}

QByteArray encodeAreYouThere(quint16 seq, quint32 localId)
{
    // remoteId is unknown at open time; it is zero until "I am here" arrives.
    return encodeControl(kOpcodeAreYouThere, seq, localId, /*remoteId=*/0);
}

QByteArray encodeAreYouReady(quint16 seq, quint32 localId, quint32 remoteId)
{
    return encodeControl(kOpcodeAreYouReady, seq, localId, remoteId);
}

QByteArray encodeIdle(quint16 seq, quint32 localId, quint32 remoteId)
{
    return encodeControl(kOpcodeIdle, seq, localId, remoteId);
}

QByteArray encodeDisconnect(quint16 seq, quint32 localId, quint32 remoteId)
{
    return encodeControl(kOpcodeDisconnect, seq, localId, remoteId);
}

QByteArray encodePing(quint16 seq, quint32 localId, quint32 remoteId,
                      quint8 reply, quint32 deviceTimeMs)
{
    QByteArray buf(kPingSize, '\0');
    writeHeader(buf, kPingSize, kOpcodePing, seq, localId, remoteId);
    buf[kOffPingReply] = static_cast<char>(reply);
    poke32le(buf, kOffPingTime, deviceTimeMs);
    return buf;
}

QByteArray encodeRetransmit(const QVector<quint16>& seqs,
                            quint32 localId, quint32 remoteId)
{
    if (seqs.isEmpty()) {
        return QByteArray();
    }

    // Single missing packet: the compact form carries the wanted seq in the
    // header's own seq field on a plain 0x10-length control packet.
    if (seqs.size() == 1) {
        return encodeControl(kOpcodeRetransmit, seqs.first(), localId, remoteId);
    }

    // Multiple: the extended form appends the sequence list after the header.
    // The header seq is 0; `len` grows to cover the appended list.  Each entry
    // occupies two little-endian bytes.
    const int listBytes = seqs.size() * 2;
    const int total = kControlSize + listBytes;
    QByteArray buf(total, '\0');
    writeHeader(buf, static_cast<quint32>(total), kOpcodeRetransmit,
                /*seq=*/0, localId, remoteId);
    int offset = kControlSize;
    for (const quint16 s : seqs) {
        poke16le(buf, offset, s);
        offset += 2;
    }
    return buf;
}

QByteArray encodeStreamPayload(quint16 seq, quint16 streamSeq,
                               quint32 localId, quint32 remoteId,
                               const QByteArray& payload)
{
    const int total = kStreamHeaderSize + payload.size();
    QByteArray buf(total, '\0');
    writeHeader(buf, static_cast<quint32>(total), /*type=*/kOpcodeIdle, seq,
                localId, remoteId);
    // Stream sub-header.  The reply marker + payload length + big-endian inner
    // sequence sit between the common header and the payload.
    buf[kOffStreamReply] = static_cast<char>(kStreamReplyMarker);
    poke16le(buf, kOffStreamDataLen, static_cast<quint16>(payload.size()));
    // NOTE: the inner sequence is BIG-endian on the wire (unlike every other
    // multi-byte field here).  PROVISIONAL, but consistently observed.
    qToBigEndian(streamSeq, reinterpret_cast<uchar*>(buf.data()) + kOffStreamSeq);
    if (!payload.isEmpty()) {
        std::memcpy(buf.data() + kOffStreamPayload, payload.constData(),
                    static_cast<size_t>(payload.size()));
    }
    return buf;
}

// --- Decoders --------------------------------------------------------------

ControlHeader parseHeader(const QByteArray& datagram)
{
    ControlHeader h;
    if (datagram.size() < kHeaderSize) {
        return h;  // valid stays false
    }
    h.len    = peek32le(datagram, kOffLen);
    h.type   = peek16le(datagram, kOffType);
    h.seq    = peek16le(datagram, kOffSeq);
    h.sentId = peek32le(datagram, kOffSentId);
    h.rcvdId = peek32le(datagram, kOffRcvdId);
    h.valid  = true;
    return h;
}

std::optional<ParsedControl> parseControl(const QByteArray& datagram)
{
    const ControlHeader header = parseHeader(datagram);
    if (!header.valid) {
        return std::nullopt;
    }

    ParsedControl out;
    out.header = header;
    out.kind = kindForOpcode(header.type);

    switch (out.kind) {
    case ControlKind::Ping: {
        // Ping needs the reply byte + 4-byte timestamp (21-byte packet).
        if (datagram.size() < kPingSize) {
            return std::nullopt;
        }
        out.pingReply = static_cast<quint8>(datagram.at(kOffPingReply));
        out.pingTime  = peek32le(datagram, kOffPingTime);
        break;
    }
    case ControlKind::Retransmit: {
        // Compact form: exactly the 16-byte header, the wanted seq is the
        // header's own seq field.
        if (datagram.size() == kControlSize) {
            out.retransmitSeqs.append(header.seq);
            break;
        }
        // Extended form: a list of little-endian u16s appended after the
        // header.  Only consume whole pairs that fall inside the buffer;
        // ignore any trailing odd byte rather than reading past the end.
        for (int off = kControlSize; off + 2 <= datagram.size(); off += 2) {
            out.retransmitSeqs.append(peek16le(datagram, off));
        }
        break;
    }
    default:
        // Idle / are-you-there / i-am-here / are-you-ready / disconnect /
        // unknown: the 16-byte header alone carries everything we need.
        break;
    }

    return out;
}

std::optional<ParsedStream> parseStreamPayload(const QByteArray& datagram)
{
    // Must be at least a full stream header (header + 5-byte sub-header).
    if (datagram.size() < kStreamHeaderSize) {
        return std::nullopt;
    }

    const ControlHeader header = parseHeader(datagram);
    if (!header.valid) {
        return std::nullopt;
    }

    const quint16 declaredLen = peek16le(datagram, kOffStreamDataLen);
    const int available = datagram.size() - kOffStreamPayload;
    if (available < 0) {
        return std::nullopt;
    }

    // Trust the smaller of "declared payload length" and "bytes actually
    // present" so a lying/truncated length can never make us read past the
    // buffer (Constitution: boundary validation).
    const int payloadLen =
        qMin(static_cast<int>(declaredLen), available);

    ParsedStream out;
    out.header = header;
    out.innerSeq = peek16be(datagram, kOffStreamSeq);
    out.payload = datagram.mid(kOffStreamPayload, payloadLen);
    out.valid = true;
    return out;
}

} // namespace AetherSDR::IcomSession
