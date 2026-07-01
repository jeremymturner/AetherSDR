# Icom IP-Remote Protocol — Clean-Room Design & Licensing-Compliance Note

Status: **planning / compliance gate**. No transport, CI-V-over-UDP, or audio
code has been written against this document yet. This note is the sign-off
gate for GitHub issue **#10** and **blocks merge** of all other Icom work
(epic **#1**; issues **#3** transport, **#4** CI-V, **#6** audio, **#7**
scope/waterfall).

This document is written to a clean-room standard. It records publicly-documented
facts and the *methodology* for deriving everything else from clean inputs.

**Provenance posture (maintainer decision, 2026-07-01): read-and-reimplement.**

Per AetherSDR Constitution Principle IV, the clean inputs are explicit and
INCLUDE open-source references. `wfview` is open-source (GPLv3), so reading it is
clean-room — it is a permitted reference alongside the public Icom CI-V reference
guides, the radio manuals, and first-party wire captures. Principle IV's
prohibition targets proprietary binaries: decompiling, disassembling, or
reverse-engineering RS-BA1 — or transcribing/translating/paraphrasing such
output — is forbidden regardless of license.

The chosen posture for the Icom protocol work is: READ `wfview` to understand the
wire format (session handshake, keepalive, sequence/retransmit, login token,
CI-V/audio UDP framing), then write AetherSDR's OWN implementation. No verbatim
copying. This keeps the implementation clean-room AND avoids entangling
AetherSDR's copyright with `wfview`'s — GPLv3→GPLv3 would permit direct attributed
reuse (preserving `wfview`'s notices + a `THIRD_PARTY_LICENSES` entry, making
those files a derivative), but we are deliberately NOT doing that, to keep
provenance simple.

Off-limits: RS-BA1 (proprietary-binary) reverse-engineering; verbatim copying of
`wfview` code.

---

## 1. Purpose & Scope

AetherSDR is adding native support for Icom transceivers over Icom's IP-remote
("Network"/KNS) protocol. The target radios and their transports are:

| Radio | Transport | Notes |
|---|---|---|
| IC-705 | Wi-Fi (station or AP mode) | Portable; only wireless IP-remote path |
| IC-9700 | Ethernet | VHF/UHF/1.2 GHz all-mode |
| IC-7610 | Ethernet | HF/6 m SDR |
| IC-7300MK2 | Ethernet | HF/6 m (network-capable successor to the USB-only IC-7300) |

The goal is a clean-room implementation of the IP-remote network protocol
these radios speak — the same protocol Icom's own **RS-BA1** desktop software
and Icom's mobile remote app use — so that AetherSDR can control the radio,
carry CI-V, stream receive/transmit audio, and render the spectrum scope over a
LAN or Wi-Fi link.

This note covers **only** the licensing hazard, the clean-room methodology, the
capture methodology, and the seed of known facts. The wire-format specification
itself is produced *separately*, by the read-and-reimplement process described in
§4, and lives in its own document — not here.

Related work:

- Epic **#1** — Icom radio support (umbrella).
- Issue **#3** — UDP transport / session layer (control port).
- Issue **#4** — CI-V command layer.
- Issue **#6** — audio streaming (RX/TX).
- Issue **#7** — spectrum scope / waterfall.
- Issue **#10** — *this* clean-room & licensing sign-off (blocks the above).

---

## 2. Licensing Hazard

There are two distinct third-party artifacts in this space, and the hazard from
each is different. It is important to state the hazard **precisely**, because the
naive framing ("wfview is GPLv3, so we can't use it") is **not** the actual
problem here — reading `wfview` is clean-room under Principle IV.

### 2.1 AetherSDR's own license

AetherSDR is licensed under the **GNU General Public License v3** (see
[`LICENSE`](../LICENSE) and the README license badge). Because AetherSDR is
*itself* GPLv3, the copyleft-incompatibility framing does **not** apply to
`wfview`: GPLv3 code is, in the abstract, license-compatible with a GPLv3
project.

So the hazard is **not** "we may not combine with GPLv3," and it is **not** "we
may not read `wfview`." Per Principle IV, reading published open-source code is
an explicitly clean input. The two genuine hazards are below.

### 2.2 Hazard A — `wfview` (GPLv3): verbatim copying (a provenance choice)

`wfview` is a mature, community-built Icom IP-remote client released under
**GPLv3**. Its `udphandler` and `udpserver` classes are the community's most
complete expression of the session/framing wrapper. Because `wfview` is
open-source, **reading it is clean-room** under Principle IV — it is a permitted
reference (see §3), and we use it to understand the wire format.

What we deliberately avoid is **verbatim copying** — importing, porting, or
transcribing `wfview` source into our tree as if we wrote it. This is a
**provenance choice**, not a Principle IV clean-room requirement:

- GPLv3→GPLv3 reuse *would be permitted* — we could copy `wfview` code directly
  if we preserved its authorship notices and added a `THIRD_PARTY_LICENSES`
  entry, making those files an attributed derivative of `wfview`. The licenses
  are compatible.
- We choose **not** to do that. Writing AetherSDR's own implementation from an
  understanding of the wire format keeps AetherSDR's copyright unentangled from
  `wfview`'s and keeps provenance simple: our Icom code is our own authorship,
  cross-checked against public docs and first-party captures.

So the posture is **read-and-reimplement**: read `wfview` to learn the framing,
then write our own code. No verbatim copying.

### 2.3 Hazard B — RS-BA1 (proprietary): reverse-engineering is forbidden

Icom's **RS-BA1** is proprietary, closed-source software. Disassembling,
decompiling, or otherwise reverse-engineering the RS-BA1 (or the Icom mobile
app) binary to recover the protocol — and transcribing, translating, or
paraphrasing any such output — is **forbidden** by Constitution Principle IV
unconditionally. This is the classic decompiled-binary case the principle
exists to refuse: such output carries RS-BA1's proprietary copyright, and
merging it would relicense Icom's work into our tree.

Observing RS-BA1 (or the mobile app) *from the outside* — i.e. capturing the
packets it exchanges with hardware we own — is a different act and is
**permitted** (see §5). We watch the wire; we do not open the binary.

### 2.4 History of this protocol (context and a permitted source)

The Icom IP-remote protocol is **undocumented by Icom** at the framing level.
Its public reverse-engineering lineage is community work (commonly credited to
the `kappanhag` effort, later carried forward and completed in `wfview`). That
open-source lineage is a **permitted reference** — reading it is clean-room —
though our authoritative cross-check for any specific detail remains first-party
capture and the public Icom docs. We read the open-source code to understand the
framing; we do not copy it verbatim.

### 2.5 Summary: forbidden vs permitted

**Forbidden**

- **Verbatim copying** of `wfview` source — importing, porting, or transcribing
  its `udphandler` / `udpserver` code, magic constants, opcode values, packet
  field offsets, or retransmit/keepalive state machine into our tree as our own
  authorship. (License-permitted with attribution, but declined for provenance.)
- Disassembling/decompiling RS-BA1 or the Icom mobile app, or using any output
  derived from doing so.

**Permitted** (detailed in §3 and §5)

- Reading `wfview` (or `kappanhag`, or other open-source clients) to understand
  the wire format — open-source references are a clean Principle IV input.
- Icom's own official public documentation (CI-V reference guides, user
  manuals).
- Public standards (ITU-T G.711 for µ-law).
- First-party packet captures of hardware **we own** exchanging traffic with
  Icom's own RS-BA1 / mobile app, observed from the outside.
- AetherSDR's own original design and implementation.

---

## 3. Permitted References

These are the clean inputs that may seed and cross-check the design. Each is
published by Icom, a public standard, an open-source reference, or a first-party
capture — all clean Principle IV inputs.

| Reference | What it legitimately provides | What it does **not** provide |
|---|---|---|
| **`wfview`** source (GPLv3, open-source) | An open-source reference for the session/framing wrapper — handshake, keepalive, sequence/retransmit, login token, CI-V/audio UDP framing. Read to *understand*; reimplement in our own words | A source to copy verbatim — no importing/porting/transcribing (§2.2, a provenance choice) |
| Icom **CI-V Reference Guide** PDFs (per model) | CI-V command/sub-command numbers; the structure of the `0x27` spectrum-scope waveform data command | The *UDP framing* that carries CI-V — CI-V guides describe the serial/CI-V command set, not the network wrapper |
| Icom **user / instruction manuals** (per model) | The three UDP **port numbers** (see §6); Network-User / KNS remote-login setup steps and terminology | Packet layouts, sequence-number schemes, handshake opcodes |
| **ITU-T G.711** | µ-law companding definition for the audio codec toward hardware | Icom's audio-session negotiation |
| **First-party pcaps** (hardware we own) | Ground truth for everything else — the actual bytes on the wire, and the authoritative cross-check for any framing detail | (see §5) |

Notes on scope of each reference:

- `wfview` is open-source (GPLv3); reading it is clean-room under Principle IV.
  We read it to understand the wire format and then write our own
  implementation. The one thing we do not do is copy it verbatim (§2.2). Where a
  specific byte layout matters, we confirm it against a first-party capture.

- The CI-V guides are published by Icom and describe the **CI-V command layer**
  only. They are safe for command numbers and for the `0x27` scope data-frame
  structure. They say nothing about how CI-V is encapsulated in UDP — that
  wrapper is derived from capture.
- The user manuals document the **KNS / Network-User** feature and the port
  numbers a user must open. They are safe for those facts and for the login
  workflow terminology. They do not document wire framing.
- G.711 is the codec reference. Audio toward the radio is **µ-law / PCM**, not
  Opus (see §6). Opus, if present anywhere, is between client and any relay, not
  toward the hardware — and that too must be confirmed by capture, not assumed.

---

## 4. Clean-Room Methodology (Read-and-Reimplement)

Issue #10 mandates a **read-and-reimplement** discipline: we may read the clean
inputs — including `wfview`'s open-source code — to *understand* the protocol,
and then we write AetherSDR's own implementation. The one bright line is
verbatim copying: no `wfview` source is imported/ported/transcribed into our
tree (§2.2), and no RS-BA1 reverse-engineering output is used at all (§2.3).

### 4.1 Understanding the protocol

The permitted inputs in §3 — first-party captures, Icom public docs, public
standards, and `wfview`'s open-source code — are all read to build an
understanding of the wire format: session handshake, keepalive, sequence and
retransmit, login token, and CI-V/audio UDP framing. Reading open-source code is
clean-room; there is no firewall against it.

Where it helps, an **independent written description** of the wire format may be
produced in AetherSDR's own words and structures. It describes the protocol as
we understand it — not copied verbatim from any client.

### 4.2 Implementing AetherSDR's own code

Implementers write AetherSDR code that is **our own authorship**. Understanding
may come from any clean input, but the code is written fresh — not by pasting or
line-by-line transcribing `wfview`. Where a specific byte layout is load-bearing,
confirm it against a first-party capture so the implementation is grounded in
observed behavior as well as the open-source reference.

### 4.3 Provenance logging

Every non-trivial protocol fact carries a provenance tag, mirroring the "Source
Provenance" discipline in
[`docs/kiwisdr-cleanroom-design.md`](kiwisdr-cleanroom-design.md):

- **`[wfview]`** — understood by reading `wfview`'s open-source (GPLv3) code.
  Clean to record; the fact is reimplemented in our own code, not copied.
- **`[icom-doc:<model> CI-V guide]`** — from an Icom CI-V reference PDF.
- **`[icom-manual:<model>]`** — from an Icom user manual.
- **`[std:G.711]`** — from the ITU-T standard.
- **`[capture:<pcap-id>@<offset>]`** — observed on the wire in a named,
  committed (redacted) capture.
- **`[design]`** — AetherSDR's own original engineering decision.

There is deliberately **no** `[rs-ba1:…]` provenance tag — RS-BA1
reverse-engineering output is not admissible (§2.3).

### 4.4 Confirming details against capture

For a session/framing detail understood from `wfview`, the authoritative
cross-check is a first-party capture:

1. **Confirm it from first-party capture where it matters.** Design a capture
   that exercises the behavior (e.g. force a retransmit by dropping a packet) and
   read the answer off our own wire. Tag it `[capture:…]` alongside `[wfview]`.
2. If a detail is still uncertain and cannot be confirmed from capture yet,
   **leave it unimplemented** rather than guess. The design records it as an open
   item ("unconfirmed — derive from capture").
3. **Never** copy `wfview` code verbatim "to unblock" — reimplement it in our own
   words, grounded in the open-source reference and confirmed by capture.

The conservative rule throughout: **it is better to ship "unconfirmed, derive
from capture" than to guess a byte layout — and better to reimplement in our own
words than to copy verbatim.**

---

## 5. Capture Methodology

First-party packet capture on hardware we own is the primary derivation source.
It is ethical and lawful here because we own the radio, we own the endpoints, we
are observing our own traffic on our own LAN, and we are not opening any
proprietary binary.

### 5.1 Setup — Ethernet radios (IC-9700 / IC-7610 / IC-7300MK2)

- Run **Wireshark** on a host on the same LAN segment as the radio.
- Because Ethernet is switched, capture from a position that actually sees the
  traffic: a **managed switch with a mirror/SPAN port**, an inline tap, or by
  running the capture host as (or beside) the RS-BA1 endpoint itself so the
  frames traverse that NIC. A plain unmanaged switch will hide unicast between
  two other hosts.
- Capture the RS-BA1 (or mobile app) ↔ radio sessions.

### 5.2 Setup — IC-705 (Wi-Fi)

- The IC-705's only IP-remote path is **Wi-Fi**. Capture on the wireless
  segment. Practical options: put the capture host on the same Wi-Fi and use a
  monitor-mode capture, or bridge the 705 through an AP/host we control so its
  frames pass a NIC we can sniff. Association keys are ours (our AP), so this is
  first-party throughout.

### 5.3 What to record

Capture enough to characterize each layer independently:

- **Control handshake** on the control port — full session bring-up from the
  first packet.
- **Keepalive cadence** — steady-state idle traffic; measure the interval and
  which side originates it.
- **Retransmit behavior** — deliberately induce loss (drop/delay a packet) and
  record how the peers detect and recover it.
- **CI-V-over-UDP framing** on the CI-V port — capture known CI-V operations
  (e.g. a frequency change) so the CI-V payload can be matched against the
  public CI-V guide and the *wrapper* around it isolated.
- **Audio-session negotiation** on the audio port — how an RX and a TX audio
  stream are set up, sample rate/codec selection, and teardown.

### 5.4 Handling credentials and committed captures

- The IP-remote login exchanges a **username/password/token** during KNS
  Network-User setup. Any captured credential material **must be redacted**
  before a capture is committed to the repo.
- Commit only the minimal frames needed as a regression fixture, with
  credentials scrubbed, and note the redaction in the capture's provenance
  entry. Never commit a raw session capture that contains live secrets.

---

## 6. What Is Known Today (Design Seed)

The following protocol knowledge seeds the design. The items in §6.1 are
documented by Icom, a public standard, or are transport-level facts. The
**session/framing wrapper** in §6.2 is understood from `wfview`'s open-source
code and confirmed against first-party capture; this document does not itself
reproduce byte layouts, but they are no longer treated as off-limits — reading
`wfview` for them is clean-room.

### 6.1 Known-good seed facts

| Fact | Value | Provenance |
|---|---|---|
| Transport is UDP (not TCP) | UDP | `[icom-manual]` + transport-level |
| Control port | **50001** | `[icom-manual]` |
| CI-V port | **50002** | `[icom-manual]` |
| Audio port | **50003** | `[icom-manual]` |
| Port 50002 payload | encapsulates standard **CI-V frames** | `[icom-manual]` + `[icom-doc CI-V guide]` |
| Audio codec toward hardware | **µ-law / PCM** (G.711), **not Opus** | `[std:G.711]` + `[icom-manual]` |
| Spectrum scope delivery | via CI-V command **`0x27`** waveform data | `[icom-doc CI-V guide]` |

### 6.2 Session/framing wrapper — **understand from `wfview`, confirm by capture**

The following are the session/framing-wrapper details. Their byte layouts are
not reproduced in this note, but they are understood by reading `wfview`'s
open-source code (`udphandler` / `udpserver`) and confirmed against first-party
capture. Each is to be reimplemented in AetherSDR's own code — never copied
verbatim:

- Packet header layout of the 50001 control-port session wrapper — **understand
  from `wfview`, confirm by capture.**
- **Sequence numbers** — field position, width, endianness, wrap behavior —
  **understand from `wfview`, confirm by capture.**
- **Retransmit / missing-packet request** packets — trigger and structure —
  **understand from `wfview`, confirm by capture.**
- Session-state opcodes (the "are-you-there" / "are-you-ready" / "ping" /
  idle-keepalive family) — **understand from `wfview`, confirm by capture.**
- **Token / login exchange** — the KNS Network-User authentication handshake and
  any session token lifecycle — **understand from `wfview`, confirm by capture.**
- How CI-V frames are *wrapped* inside 50002 datagrams (the framing around the
  CI-V payload, as opposed to the CI-V payload itself) — **understand from
  `wfview`, confirm by capture.**
- Audio-session negotiation on 50003 — sample rate, packetization, RX vs TX
  stream setup and teardown — **understand from `wfview`, confirm by capture.**

Reading `wfview` (`udphandler` / `udpserver` and its constants) for these is
clean-room; **do not copy it verbatim**, and do not use RS-BA1
reverse-engineering output for any of them.

---

## 7. Compliance Checklist / Sign-Off Gate

Issue #10 blocks merge of #3/#4/#6/#7 until every box below is checked. This is
the licensing sign-off gate.

- [ ] **AetherSDR's own implementation written** from an understanding of the
      clean inputs (first-party captures + Icom public docs + public standards +
      `wfview`'s open-source code), with per-fact provenance tags (§4.3).
- [ ] **Capture methodology documented** and first-party captures taken on
      owned hardware, with credentials redacted from anything committed (§5).
- [ ] **Licensing sign-off recorded before** the transport (#3) or CI-V (#4)
      layers merge — no Icom transport/CI-V code lands ahead of this gate.
- [ ] **`THIRD_PARTY_LICENSES` reviewed / updated** and authorship notices
      accurate — reflecting that `wfview` was read as a reference and
      reimplemented (no verbatim `wfview` code incorporated), consistent with the
      form used for FlexLib and KiwiSDR.
- [ ] **No verbatim `wfview` copying** — no ported code, no transcribed
      `udphandler` / `udpserver` source, magic constants, opcode values, or
      packet field offsets copied as-is; understood details are reimplemented in
      our own code. (Reading `wfview` is permitted; `[wfview]` provenance tags
      are fine.)
- [ ] **No RS-BA1-derived material** — no disassembly output or anything
      transcribed/paraphrased from it; provenance log contains no `[rs-ba1:…]`
      entries.
- [ ] **Every session/framing item in §6.2** is reimplemented in our own code,
      confirmed against a committed capture where load-bearing, or explicitly
      left unimplemented — none copied verbatim from `wfview`, none guessed.

Until all boxes are checked, the Icom transport, CI-V, audio, and scope PRs stay
blocked.

---

## 8. Constitution Mapping

This work is governed by the AetherSDR Constitution
([`CONSTITUTION.md`](../CONSTITUTION.md) /
[`.specify/memory/constitution.md`](../.specify/memory/constitution.md)).

### 8.1 Principle IV — Every Contribution Is Clean-Room

Principle IV is the controlling principle for issue #10. Its text
([`CONSTITUTION.md`](../CONSTITUTION.md) §IV):

> Every contribution is clean-room from start to finish. Its code, and the
> protocol knowledge behind it, must come from clean sources: public
> documentation, open-source references, behavior observed on the wire, and the
> contributor's own design and implementation. Code that is decompiled,
> disassembled, or otherwise reverse-engineered from a proprietary binary — or
> transcribed, translated, or paraphrased from such output — must never enter the
> codebase, however correct or convenient it is.

And, on what counts as clean:

> The clean inputs are explicit. Reading FlexLib's published open-source code
> (Principle I), capturing and studying the protocol as it actually behaves on
> the wire, and reading official or public documentation are all clean-room.

The clean inputs are explicit and **include open-source references**. The
prohibition targets **proprietary binaries** (decompile / disassemble /
reverse-engineer, or transcribe/translate/paraphrase such output).

Applied here:

- **Open-source references** — reading `wfview` (GPLv3) is an explicitly clean
  input, on the same footing as reading FlexLib's published code. We read it to
  understand the wire format (§3, §4).
- **Observed-on-the-wire** captures of our own hardware are an explicitly clean
  input — this is the §5 methodology, and our authoritative cross-check.
- **RS-BA1 disassembly** is the exact prohibited case (proprietary binary) —
  forbidden (§2.3).
- **Verbatim `wfview` copying** is *not* a Principle IV violation (the licenses
  are compatible and reading it is clean), but we decline it as a **provenance
  choice** (§2.2): we reimplement in our own code to keep AetherSDR's copyright
  unentangled from `wfview`'s.

### 8.2 Principle VII — Untrusted Input Is Validated At The Boundary

The new Icom UDP parsers (50001 / 50002 / 50003) are exactly the kind of
external byte stream Principle VII governs, and the radio link — especially the
IC-705 over Wi-Fi and any WAN-reachable KNS setup — is reachable beyond
localhost. Each parser must **bounds-check lengths, cap allocations, validate
ranges, and fail closed on malformed input**, validating once at the boundary
where bytes enter. A truncated datagram, an oversized field, or an out-of-range
sequence index is an *expected* input, not an exceptional one; the parser must
not crash, hang, over-allocate, or act on bad data.

### 8.3 Principle VI — AetherSDR Never Transmits Without Operator Intent

The Icom path can key the transmitter (CI-V TX commands and TX audio on 50003).
Principle VI applies in full: AetherSDR must **never** cause an Icom emission the
operator did not deliberately initiate. No keepalive, session-resync,
retransmit, or state-recovery path may key the radio as a side effect. Every
Icom transmission must trace to a deliberate operator action (PTT, an explicit
tune request, an operator-started keyer/beacon), and any TX code path fails
closed if intent is not unambiguous.

### 8.4 Principles II / III — The Radio Is Authoritative

Consistent with Principles II (the radio is authoritative on live state) and III
(radio-persistable settings live on the radio), AetherSDR treats the Icom radio
as the source of truth for its live state and saved settings. AetherSDR reflects
what the radio reports rather than asserting a divergent local view, and it does
not shadow-persist settings that belong on the radio.

---

## 9. Non-Goals For This Note

- This note does **not** contain the wire-format specification. That is produced
  separately, from the clean inputs (§3, §4), in its own document.
- This note does **not** reproduce any session/framing byte layout — those are
  understood from `wfview` and confirmed by capture during implementation (§6.2).
- This note does **not** authorize verbatim copying of `wfview` code, or any
  reverse-engineering of RS-BA1. (Reading `wfview` is permitted and clean-room.)
