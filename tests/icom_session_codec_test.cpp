#include "core/IcomSessionCodec.h"

#include <QByteArray>
#include <QVector>

#include <cstdio>

// Freestanding test for the Icom session/framing codec (issue #3).  No test
// framework: returns 0 on success, 1 on the first failure with a stderr note.
// Links Qt6::Core only.  Mirrors the KiwiSdrProtocol freestanding-test style.

using namespace AetherSDR::IcomSession;

namespace {

int fail(const char* message)
{
    std::fprintf(stderr, "icom_session_codec_test: %s\n", message);
    return 1;
}

// Read a little-endian u16/u32 straight out of a datagram so the test does not
// depend on the codec's own accessors when checking encoded layout.
quint16 rawU16le(const QByteArray& b, int off)
{
    return static_cast<quint16>(static_cast<quint8>(b.at(off)))
         | static_cast<quint16>(static_cast<quint8>(b.at(off + 1))) << 8;
}

quint32 rawU32le(const QByteArray& b, int off)
{
    return static_cast<quint32>(static_cast<quint8>(b.at(off)))
         | static_cast<quint32>(static_cast<quint8>(b.at(off + 1))) << 8
         | static_cast<quint32>(static_cast<quint8>(b.at(off + 2))) << 16
         | static_cast<quint32>(static_cast<quint8>(b.at(off + 3))) << 24;
}

} // namespace

int main()
{
    constexpr quint32 kLocalId = 0xDEADBEEFu;
    constexpr quint32 kRemoteId = 0x12345678u;

    // --- Control-packet layout + build/parse round-trips -------------------

    // Are-you-there: opcode 0x03, remoteId zero at open time.
    {
        const QByteArray dg = encodeAreYouThere(0x0001, kLocalId);
        if (dg.size() != kControlSize) {
            return fail("are-you-there wrong size");
        }
        if (rawU32le(dg, 0x00) != static_cast<quint32>(kControlSize)) {
            return fail("are-you-there len field wrong");
        }
        if (rawU16le(dg, 0x04) != kOpcodeAreYouThere) {
            return fail("are-you-there opcode wrong");
        }
        if (rawU16le(dg, 0x06) != 0x0001) {
            return fail("are-you-there seq wrong");
        }
        if (rawU32le(dg, 0x08) != kLocalId) {
            return fail("are-you-there sentid wrong");
        }
        if (rawU32le(dg, 0x0C) != 0u) {
            return fail("are-you-there rcvdid should be zero");
        }

        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::AreYouThere) {
            return fail("are-you-there did not parse to AreYouThere");
        }
        if (parsed->header.sentId != kLocalId || parsed->header.seq != 0x0001) {
            return fail("are-you-there parsed header fields wrong");
        }
    }

    // I-am-here (built as a generic control, opcode 0x04) must parse back and
    // expose the radio's session id in sentId — the field the transport
    // latches as its remote id.
    {
        const QByteArray dg =
            encodeControl(kOpcodeIAmHere, 0x0002, kRemoteId, kLocalId);
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::IAmHere) {
            return fail("i-am-here did not parse to IAmHere");
        }
        if (parsed->header.sentId != kRemoteId) {
            return fail("i-am-here sentId (radio id) wrong");
        }
        if (parsed->header.rcvdId != kLocalId) {
            return fail("i-am-here rcvdId (our id echoed) wrong");
        }
    }

    // Are-you-ready round-trip.
    {
        const QByteArray dg = encodeAreYouReady(0x0003, kLocalId, kRemoteId);
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::AreYouReady) {
            return fail("are-you-ready did not parse");
        }
        if (parsed->header.rcvdId != kRemoteId) {
            return fail("are-you-ready remote id wrong");
        }
    }

    // Idle keepalive round-trip.
    {
        const QByteArray dg = encodeIdle(0x00FF, kLocalId, kRemoteId);
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::Idle) {
            return fail("idle did not parse");
        }
        if (parsed->header.seq != 0x00FF) {
            return fail("idle seq wrong");
        }
    }

    // Disconnect round-trip.
    {
        const QByteArray dg = encodeDisconnect(0x0004, kLocalId, kRemoteId);
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::Disconnect) {
            return fail("disconnect did not parse");
        }
    }

    // --- Ping build/parse round-trip (reply flag + timestamp) --------------
    {
        constexpr quint32 kTime = 0x0A0B0C0Du;
        const QByteArray dg =
            encodePing(0x0005, kLocalId, kRemoteId, /*reply=*/0, kTime);
        if (dg.size() != kPingSize) {
            return fail("ping wrong size");
        }
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::Ping) {
            return fail("ping did not parse");
        }
        if (parsed->pingReply != 0) {
            return fail("ping reply flag wrong");
        }
        if (parsed->pingTime != kTime) {
            return fail("ping timestamp round-trip failed");
        }

        // A ping reply (flag = 1) must survive too.
        const QByteArray reply =
            encodePing(0x0005, kLocalId, kRemoteId, /*reply=*/1, kTime);
        const auto parsedReply = parseControl(reply);
        if (!parsedReply || parsedReply->pingReply != 1) {
            return fail("ping reply flag round-trip failed");
        }
    }

    // --- Retransmit request: single + range --------------------------------
    {
        // Single missing seq -> compact 0x10 form, seq in header.
        QVector<quint16> one;
        one.append(0x0042);
        const QByteArray dg = encodeRetransmit(one, kLocalId, kRemoteId);
        if (dg.size() != kControlSize) {
            return fail("single retransmit wrong size");
        }
        const auto parsed = parseControl(dg);
        if (!parsed || parsed->kind != ControlKind::Retransmit) {
            return fail("single retransmit did not parse");
        }
        if (parsed->retransmitSeqs.size() != 1
            || parsed->retransmitSeqs.first() != 0x0042) {
            return fail("single retransmit seq wrong");
        }

        // Range -> extended form, list appended after header.
        QVector<quint16> many;
        many.append(0x0010);
        many.append(0x0011);
        many.append(0x0013);
        const QByteArray dgMany = encodeRetransmit(many, kLocalId, kRemoteId);
        if (dgMany.size() != kControlSize + 3 * 2) {
            return fail("range retransmit wrong size");
        }
        const auto parsedMany = parseControl(dgMany);
        if (!parsedMany || parsedMany->kind != ControlKind::Retransmit) {
            return fail("range retransmit did not parse");
        }
        if (parsedMany->retransmitSeqs != many) {
            return fail("range retransmit list round-trip failed");
        }

        // Empty list -> empty datagram.
        if (!encodeRetransmit(QVector<quint16>(), kLocalId, kRemoteId).isEmpty()) {
            return fail("empty retransmit should produce empty datagram");
        }
    }

    // --- Stream envelope wrap+unwrap: a CI-V frame must survive -------------
    {
        // A representative CI-V frame: FE FE <to> <from> <cmd> ... FD.
        const QByteArray civ =
            QByteArray::fromHex("fefe94e01903fd");
        const quint16 streamSeq = 0x00AB;
        const QByteArray dg =
            encodeStreamPayload(0x0006, streamSeq, kLocalId, kRemoteId, civ);
        if (dg.size() != kStreamHeaderSize + civ.size()) {
            return fail("stream envelope wrong size");
        }
        // The declared inner length is little-endian at 0x11.
        if (rawU16le(dg, 0x11) != static_cast<quint16>(civ.size())) {
            return fail("stream envelope datalen wrong");
        }
        // The inner sequence is BIG-endian at 0x13.
        const quint16 seqBe =
            static_cast<quint16>(static_cast<quint8>(dg.at(0x13))) << 8
            | static_cast<quint16>(static_cast<quint8>(dg.at(0x14)));
        if (seqBe != streamSeq) {
            return fail("stream envelope inner seq not big-endian");
        }

        const auto parsed = parseStreamPayload(dg);
        if (!parsed || !parsed->valid) {
            return fail("stream envelope did not parse");
        }
        if (parsed->innerSeq != streamSeq) {
            return fail("stream inner seq round-trip failed");
        }
        if (parsed->payload != civ) {
            return fail("CI-V frame did not survive wrap->unwrap");
        }
        if (parsed->header.sentId != kLocalId
            || parsed->header.rcvdId != kRemoteId) {
            return fail("stream envelope ids wrong");
        }
    }

    // Empty-payload stream frame: should parse with an empty payload.
    {
        const QByteArray dg =
            encodeStreamPayload(0x0007, 0x0001, kLocalId, kRemoteId,
                                QByteArray());
        const auto parsed = parseStreamPayload(dg);
        if (!parsed || !parsed->valid || !parsed->payload.isEmpty()) {
            return fail("empty-payload stream frame mishandled");
        }
    }

    // --- Malformed / truncated input safety --------------------------------
    {
        // Empty buffer: every parser must reject without indexing.
        if (parseHeader(QByteArray()).valid) {
            return fail("parseHeader accepted empty buffer");
        }
        if (parseControl(QByteArray())) {
            return fail("parseControl accepted empty buffer");
        }
        if (parseStreamPayload(QByteArray())) {
            return fail("parseStreamPayload accepted empty buffer");
        }

        // One byte short of a header.
        const QByteArray shortHdr(kHeaderSize - 1, '\0');
        if (parseControl(shortHdr)) {
            return fail("parseControl accepted sub-header buffer");
        }

        // A ping opcode but truncated before the timestamp: must be rejected,
        // never read past the end.
        QByteArray badPing = encodePing(1, kLocalId, kRemoteId, 0, 0x11223344u);
        badPing.chop(3);  // drop part of the 4-byte timestamp
        if (parseControl(badPing)) {
            return fail("parseControl accepted truncated ping");
        }

        // A stream frame whose declared inner length overruns the datagram:
        // the parser must clamp to the bytes actually present, not overrun.
        QByteArray liar = encodeStreamPayload(1, 1, kLocalId, kRemoteId,
                                              QByteArray::fromHex("aabb"));
        // Overwrite the declared datalen at 0x11 with a huge value.
        liar[0x11] = static_cast<char>(0xFF);
        liar[0x12] = static_cast<char>(0xFF);
        const auto clamped = parseStreamPayload(liar);
        if (!clamped || !clamped->valid) {
            return fail("stream parser rejected clampable frame");
        }
        if (clamped->payload.size() != liar.size() - kStreamHeaderSize) {
            return fail("stream parser did not clamp overrunning datalen");
        }

        // A stream frame that is exactly the header (no payload room reported)
        // still parses with an empty payload rather than reading past the end.
        const QByteArray hdrOnly(kStreamHeaderSize, '\0');
        const auto edge = parseStreamPayload(hdrOnly);
        if (!edge || !edge->valid || !edge->payload.isEmpty()) {
            return fail("header-only stream frame mishandled");
        }

        // One byte short of a stream header must be rejected.
        const QByteArray shortStream(kStreamHeaderSize - 1, '\0');
        if (parseStreamPayload(shortStream)) {
            return fail("parseStreamPayload accepted sub-header stream buffer");
        }
    }

    // --- Sequence-field handling -------------------------------------------
    {
        // Sequence numbers must round-trip untouched across the whole u16 range
        // sample, including the high bit.
        const quint16 seqs[] = {0x0000, 0x0001, 0x00FF, 0x0100, 0x7FFF,
                                0x8000, 0xFFFF};
        for (const quint16 s : seqs) {
            const QByteArray dg = encodeIdle(s, kLocalId, kRemoteId);
            const auto parsed = parseControl(dg);
            if (!parsed || parsed->header.seq != s) {
                return fail("control seq did not round-trip");
            }
            const QByteArray sdg =
                encodeStreamPayload(0, s, kLocalId, kRemoteId,
                                    QByteArray::fromHex("00"));
            const auto sparsed = parseStreamPayload(sdg);
            if (!sparsed || sparsed->innerSeq != s) {
                return fail("stream inner seq did not round-trip");
            }
        }
    }

    // --- passcode() username/password obfuscation --------------------------
    // These vectors are computed straight from the verified substitution table
    // (kappanhang passcode.go / wfview), the one part of this protocol confirmed
    // against real hardware.  They guard the table + the index/wrap arithmetic.
    {
        // "a" (0x61) at index 0 -> table['a'] = 0x38, then zero-padded to 16.
        const QByteArray a = passcode(QStringLiteral("a"));
        if (a.size() != 16) {
            return fail("passcode output must be 16 bytes");
        }
        if (static_cast<quint8>(a[0]) != 0x38) {
            return fail("passcode(\"a\") first byte wrong");
        }
        for (int i = 1; i < 16; ++i) {
            if (a[i] != '\0') {
                return fail("passcode must zero-pad the tail");
            }
        }

        // "ab": 'a'@0 -> 0x38; 'b'(0x62)@1 -> table[0x63] = 0x2e (index math).
        const QByteArray ab = passcode(QStringLiteral("ab"));
        if (static_cast<quint8>(ab[0]) != 0x38
            || static_cast<quint8>(ab[1]) != 0x2e) {
            return fail("passcode(\"ab\") wrong");
        }

        // Wrap path (char+index > 126): "~~~~" -> 0x52,0x47,0x5d,0x4c.
        const QByteArray tilde = passcode(QStringLiteral("~~~~"));
        if (static_cast<quint8>(tilde[0]) != 0x52
            || static_cast<quint8>(tilde[1]) != 0x47
            || static_cast<quint8>(tilde[2]) != 0x5d
            || static_cast<quint8>(tilde[3]) != 0x4c) {
            return fail("passcode wrap-around arithmetic wrong");
        }

        // Never emits more than 16 bytes even for a long input.
        if (passcode(QStringLiteral("0123456789ABCDEFGHIJ")).size() != 16) {
            return fail("passcode must cap at 16 bytes");
        }
    }

    return 0;
}
