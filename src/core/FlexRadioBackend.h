#pragma once

// FlexRadioBackend — the FlexRadio (SmartSDR) side of the RadioBackend seam
// (issue #2 / epic #1).
//
// STATUS: ADDITIVE SCAFFOLDING.  This class proves the RadioBackend interface
// is implementable for Flex by wrapping the existing `RadioConnection` (raw
// TCP, V/H/R/S/M).  It is NOT yet consumed by `RadioModel` — RadioModel still
// drives `RadioConnection` directly.  Wiring RadioModel to talk to backends
// through this seam (and moving Flex media through it) is the #2 follow-up.
// Until then, nothing in the running app constructs this class.
//
// Scope of this first cut: control intents + status forwarding only.
//   * Control intents (setFrequency/setMode/setFilterBandwidth/refreshState)
//     are serialized to SmartSDR text and written via
//     `RadioConnection::writeCommand(seq, cmd)`, using a monotonic sequence.
//   * `statusReceived` is forwarded from the connection VERBATIM — Flex is
//     already the normalized (object, key/value) shape the interface documents,
//     so no translation is needed (unlike the Icom backend).
//   * The RX media signals (`audioDataReady`/`spectrumReady`/
//     `waterfallRowReady`) are intentionally NOT emitted here.  In the real app
//     Flex media flows from `PanadapterStream`, which `RadioModel` owns — NOT
//     from `RadioConnection`.  Rather than fabricate a media path, we leave Flex
//     media in RadioModel until the RadioModel<->RadioBackend rewire (#2
//     follow-up) can move it across the seam cleanly.

#include "RadioBackend.h"
#include "RadioDiscovery.h"

#include <QString>

#include <atomic>
#include <cstdint>

namespace AetherSDR {

class RadioConnection;

// RadioBackend implementation for FlexRadio (SmartSDR protocol).  Owns a
// `RadioConnection` (parented to `this`, so they move to a worker thread
// together per #502) and translates high-level control intents into SmartSDR
// command strings.
class FlexRadioBackend : public RadioBackend {
    Q_OBJECT

public:
    explicit FlexRadioBackend(QObject* parent = nullptr);
    ~FlexRadioBackend() override;

    RadioType type() const override { return RadioType::Flex; }
    bool isConnected() const override;
    QString displayName() const override { return m_displayName; }

public slots:
    void connectToRadio(const RadioInfo& info) override;
    void disconnectFromRadio() override;
    void setFrequency(int receiver, quint64 hz) override;
    void setMode(int receiver, const QString& mode) override;
    void setFilterBandwidth(int receiver, int lowHz, int highHz) override;
    void refreshState() override;

private:
    // Allocate the next monotonic command sequence for writeCommand().
    quint32 nextSeq();

    RadioConnection* m_conn{nullptr};
    QString m_displayName;  // model/nickname captured from connectToRadio()
    std::atomic<quint32> m_seqCounter{1};
};

} // namespace AetherSDR
