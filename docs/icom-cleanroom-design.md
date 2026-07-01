# Icom IP-Remote Protocol — Clean-Room Design & Licensing-Compliance Note

Status: **planning / compliance gate**. No transport, CI-V-over-UDP, or audio
code has been written against this document yet. This note is the sign-off
gate for GitHub issue **#10** and **blocks merge** of all other Icom work
(epic **#1**; issues **#3** transport, **#4** CI-V, **#6** audio, **#7**
scope/waterfall).

This document is itself written to a clean-room standard. It records only
publicly-documented facts and the *methodology* for deriving everything else
from first-party packet captures of hardware we own. It deliberately does
**not** reproduce, transcribe, or paraphrase any protocol detail whose only
known source is the GPLv3 `wfview` project (its `udphandler` / `udpserver`
classes in particular) or any RS-BA1 disassembly. Every session/framing detail
in that category is marked **TO BE DERIVED FROM CAPTURE — do not import**.

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
capture methodology, and the seed of publicly-known facts. The wire-format
specification itself is produced *separately*, by the two-room process described
in §4, and lives in its own independent spec document — not here.

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
each is different. It is important to state the hazard **precisely**, because
the naive framing ("wfview is GPLv3, so we can't use it") is not the actual
problem here.

### 2.1 AetherSDR's own license

AetherSDR is licensed under the **GNU General Public License v3** (see
[`LICENSE`](../LICENSE) and the README license badge). Because AetherSDR is
*itself* GPLv3, the copyleft-incompatibility framing does **not** apply to
`wfview`: GPLv3 code is, in the abstract, license-compatible with a GPLv3
project.

So the hazard is **not** "we may not combine with GPLv3." The hazards are the
two below, and they bind regardless of AetherSDR's own license.

### 2.2 Hazard A — `wfview` (GPLv3): provenance, attribution, and Principle IV

`wfview` is a mature, community-built Icom IP-remote client released under
**GPLv3**. Its `udphandler` and `udpserver` classes are the community's most
complete expression of the session/framing wrapper.

Even though the *licenses* are compatible, importing, porting, translating, or
closely paraphrasing `wfview` source — including copying its magic constants,
opcode enumerations, packet-struct field layouts, or retransmit state machine —
is **forbidden** here, for these reasons:

- **Constitution Principle IV** (§8) requires every contribution to be
  clean-room *from start to finish*, built from the contributor's own design.
  Code lifted from another project fails that standard on its face, and
  Principle IV's "contamination travels" rule means anything written by reading
  such code is also tainted.
- **Copyright and attribution** remain with `wfview`'s authors. Absorbing their
  authored code into AetherSDR without carrying its authorship and complying
  with GPLv3's source/attribution obligations is a copyright problem *even
  between two GPLv3 projects*. The right way to reuse GPLv3 code is to depend on
  it as an attributed, separately-licensed component — not to silently
  transcribe its internals into our tree as if we wrote them.
- **Clean provenance is the deliverable of issue #10.** The whole point of this
  gate is that AetherSDR's Icom protocol knowledge is traceable to first-party
  captures and public docs, not to reading someone else's client.

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

### 2.4 History of this protocol (context, not a source)

The Icom IP-remote protocol is **undocumented by Icom** at the framing level.
Its public reverse-engineering lineage is community work (commonly credited to
the `kappanhag` effort, later carried forward and completed in `wfview`). That
history is context for *why* the framing is only "known" through GPLv3 code —
which is exactly why we treat every framing detail as tainted until we
re-derive it ourselves from capture. The lineage is **not** a permitted source:
we do not read those repositories to fill in byte layouts.

### 2.5 Summary: forbidden vs permitted

**Forbidden**

- Copying, porting, translating, or closely paraphrasing any `wfview` source,
  including `udphandler` / `udpserver`, their magic constants, opcode values,
  packet field offsets, or retransmit/keepalive state machine.
- Reading `wfview` (or `kappanhag`, or any port of them) to learn the framing
  and then writing "our own" version of what we just read.
- Disassembling/decompiling RS-BA1 or the Icom mobile app, or using any output
  derived from doing so.

**Permitted** (detailed in §3 and §5)

- Icom's own official public documentation (CI-V reference guides, user
  manuals).
- Public standards (ITU-T G.711 for µ-law).
- First-party packet captures of hardware **we own** exchanging traffic with
  Icom's own RS-BA1 / mobile app, observed from the outside.
- AetherSDR's own original design and implementation built from an independent
  spec.

---

## 3. Permitted Public References

These are the clean inputs that may seed and cross-check the independent spec.
Each is either published by Icom, a public standard, or a first-party capture.

| Reference | What it legitimately provides | What it does **not** provide |
|---|---|---|
| Icom **CI-V Reference Guide** PDFs (per model) | CI-V command/sub-command numbers; the structure of the `0x27` spectrum-scope waveform data command | The *UDP framing* that carries CI-V — CI-V guides describe the serial/CI-V command set, not the network wrapper |
| Icom **user / instruction manuals** (per model) | The three UDP **port numbers** (see §6); Network-User / KNS remote-login setup steps and terminology | Packet layouts, sequence-number schemes, handshake opcodes |
| **ITU-T G.711** | µ-law companding definition for the audio codec toward hardware | Icom's audio-session negotiation |
| **First-party pcaps** (hardware we own) | Ground truth for everything else — the actual bytes on the wire | (this is the primary derivation source, see §5) |

Notes on scope of each reference:

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

## 4. Clean-Room Methodology (Two-Room Discipline)

Issue #10 mandates a **two-room** separation between deriving the protocol and
implementing it. This is the standard clean-room firewall.

### 4.1 Room A — Spec authors

Spec authors produce an **independent, written protocol specification** from
**only** the permitted inputs in §3: first-party captures plus Icom public docs
plus public standards. They may look at the raw pcaps and at Icom PDFs. They
**may not** read `wfview` / `kappanhag` source or RS-BA1 disassembly, and they
may not accept any protocol fact whose only provenance is that code.

The spec they write is the sole interface between the rooms. It describes the
wire format in AetherSDR's own words and structures, derived from observed
bytes — not copied from any client.

### 4.2 Room B — Implementers

Implementers write AetherSDR code **from the Room A spec only**. They do not
consult `wfview`, RS-BA1, or captures-plus-reverse-engineering; they consult the
independent spec. If the spec is silent or ambiguous, the answer is to send the
question back to Room A for re-derivation from capture — never to "just check
wfview."

### 4.3 Provenance logging

Every protocol fact that enters the spec carries a provenance tag, mirroring the
"Source Provenance" discipline in
[`docs/kiwisdr-cleanroom-design.md`](kiwisdr-cleanroom-design.md):

- **`[icom-doc:<model> CI-V guide]`** — from an Icom CI-V reference PDF.
- **`[icom-manual:<model>]`** — from an Icom user manual.
- **`[std:G.711]`** — from the ITU-T standard.
- **`[capture:<pcap-id>@<offset>]`** — observed on the wire in a named,
  committed (redacted) capture.
- **`[design]`** — AetherSDR's own original engineering decision.

A fact with no clean provenance tag does not enter the spec. There is
deliberately **no** `[wfview:…]` or `[rs-ba1:…]` provenance tag — those sources
are not admissible.

### 4.4 Handling "tainted" details

A protocol detail is **tainted** if the only place it is currently "known" is
GPLv3 `wfview` source or RS-BA1 disassembly. When Room A hits a tainted detail:

1. **Re-derive it from first-party capture.** Design a capture that exercises
   the behavior (e.g. force a retransmit by dropping a packet) and read the
   answer off our own wire. Tag it `[capture:…]`.
2. If it genuinely **cannot** be re-derived from capture yet, **leave it
   unimplemented.** The spec records it as an open item ("unknown — derive from
   capture"); Room B does not implement around a guess.
3. **Never** import the tainted value "to unblock." A tainted constant that
   enters the tree contaminates everything built on it and forces a rip-out.

The conservative rule throughout: **it is better to ship "unknown, derive from
capture" than to guess — or import — a byte layout.**

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

## 6. What Is Safely Known Publicly Today (Spec Seed)

The following is the **only** protocol knowledge that may seed the spec without
a fresh capture, because each item is documented by Icom, a public standard, or
is a transport-level fact. Everything about the **session/framing wrapper** is
explicitly deferred to capture — this document does **not** fill in byte
layouts.

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

### 6.2 Explicitly deferred — **TO BE DERIVED FROM CAPTURE (do not import)**

The following are the session/framing-wrapper details. Their byte layouts are
**not** written here, because their only current "known" source is GPLv3 code.
Each is an open item for Room A to derive from first-party capture:

- Packet header layout of the 50001 control-port session wrapper — **TO BE
  DERIVED FROM CAPTURE.**
- **Sequence numbers** — field position, width, endianness, wrap behavior —
  **TO BE DERIVED FROM CAPTURE.**
- **Retransmit / missing-packet request** packets — trigger and structure —
  **TO BE DERIVED FROM CAPTURE.**
- Session-state opcodes (the "are-you-there" / "are-you-ready" / "ping" /
  idle-keepalive family) — **TO BE DERIVED FROM CAPTURE.**
- **Token / login exchange** — the KNS Network-User authentication handshake and
  any session token lifecycle — **TO BE DERIVED FROM CAPTURE.**
- How CI-V frames are *wrapped* inside 50002 datagrams (the framing around the
  CI-V payload, as opposed to the CI-V payload itself) — **TO BE DERIVED FROM
  CAPTURE.**
- Audio-session negotiation on 50003 — sample rate, packetization, RX vs TX
  stream setup and teardown — **TO BE DERIVED FROM CAPTURE.**

Do **not** fill any of these in from `wfview` (`udphandler` / `udpserver` or its
constants) or from RS-BA1. They are derived from our own captures or they stay
unimplemented.

---

## 7. Compliance Checklist / Sign-Off Gate

Issue #10 blocks merge of #3/#4/#6/#7 until every box below is checked. This is
the licensing sign-off gate.

- [ ] **Independent protocol spec written** by Room A from first-party captures
      + Icom public docs + public standards only, with per-fact provenance tags
      (§4.3) and no `wfview`/RS-BA1-derived material.
- [ ] **Capture methodology documented** and first-party captures taken on
      owned hardware, with credentials redacted from anything committed (§5).
- [ ] **Licensing sign-off recorded before** the transport (#3) or CI-V (#4)
      layers merge — no Icom transport/CI-V code lands ahead of this gate.
- [ ] **`THIRD_PARTY_LICENSES` reviewed / updated** to reflect the Icom protocol
      reference situation, in the same "protocol facts consulted, no code
      incorporated" form already used for FlexLib and KiwiSDR.
- [ ] **No `wfview`-derived material** — no ported code, no copied magic
      constants, opcode values, or packet field offsets from `udphandler` /
      `udpserver`; provenance log contains no `[wfview:…]` entries.
- [ ] **No RS-BA1-derived material** — no disassembly output or anything
      transcribed/paraphrased from it; provenance log contains no `[rs-ba1:…]`
      entries.
- [ ] **Every deferred item in §6.2** is either derived from a committed capture
      or explicitly left unimplemented — none guessed or imported.

Until all boxes are checked, the Icom transport, CI-V, audio, and scope PRs stay
blocked.

---

## 8. Constitution Mapping

This work is governed by the AetherSDR Constitution
([`CONSTITUTION.md`](../CONSTITUTION.md) /
[`.specify/memory/constitution.md`](../.specify/memory/constitution.md)).

### 8.1 Principle IV — Every Contribution Is Clean-Room

Principle IV is the controlling principle for issue #10. It requires every
contribution — code *and the protocol knowledge behind it* — to come from clean
sources: public documentation, license-compatible open-source references,
behavior observed on the wire, and the contributor's own design. It bars any
code that is "decompiled, disassembled, or otherwise reverse-engineered from a
proprietary binary — or transcribed, translated, or paraphrased from such
output," and it states that contamination *travels to everything written by
reading it*.

Applied here:

- **Observed-on-the-wire** captures of our own hardware are an explicitly clean
  input — this is the §5 methodology.
- **RS-BA1 disassembly** is the exact prohibited case (proprietary binary) —
  forbidden (§2.3).
- **`wfview` porting/paraphrasing** fails the "contributor's own design" and
  "contamination travels" standard, so it is refused at the door even though the
  licenses are compatible (§2.2). Refusing it costs almost nothing now; undoing
  contamination later means ripping out everything built on it.

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

- This note does **not** contain the wire-format specification. That is the
  independent Room A deliverable (§4) in its own document.
- This note does **not** provide any session/framing byte layout — those are
  deferred to capture (§6.2).
- This note does **not** authorize reading `wfview` or RS-BA1 for any purpose.
