#pragma once

#include "RadioType.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVector>

namespace AetherSDR {

struct RadioInfo;  // RadioDiscovery.h

// Abstract transport/protocol backend for "a radio".
//
// AetherSDR was built entirely around the FlexRadio SmartSDR protocol:
// `RadioConnection` (raw TCP) and `WanConnection` (SmartLink/TLS) both speak
// V/H/R/S/M and happen to emit an identical signal set, which `RadioModel`
// consumes by checking whichever is alive.  That informal duality is the seam
// this interface formalizes (issue #2 / epic #1) so a second protocol backend
// (Icom, issue #3+) can be dropped in without `RadioModel` knowing the wire
// format.
//
// Design notes:
//   * The RX media signals (`audioDataReady`, `spectrumReady`,
//     `waterfallRowReady`) are deliberately format-agnostic — `AudioEngine`
//     already consumes generic PCM and `PanadapterModel` already consumes
//     generic `QVector<float>` dBm bins, regardless of source (the KiwiSDR
//     path proves this).  A backend converts its native wire format to these
//     shapes internally.
//   * `statusReceived` carries normalized object/key-value state.  The Flex
//     backend forwards parsed S-lines verbatim; the Icom backend translates
//     decoded CI-V responses into the same shape so `RadioModel` stays
//     vendor-neutral.
//   * Control is exposed as high-level INTENTS (setFrequency/setMode/…), not a
//     raw command string, so `IcomBackend` never has to parse SmartSDR text.
//     `FlexRadioBackend` serializes each intent to SmartSDR; a Flex-only raw
//     escape hatch lives on that subclass during migration, not here.
//
// Backends are designed to live on a worker thread like `RadioConnection`
// (#502); construct, `moveToThread()`, then drive via queued slot calls.
class RadioBackend : public QObject {
    Q_OBJECT

public:
    explicit RadioBackend(QObject* parent = nullptr) : QObject(parent) {}
    ~RadioBackend() override = default;

    RadioBackend(const RadioBackend&) = delete;
    RadioBackend& operator=(const RadioBackend&) = delete;

    virtual RadioType type() const = 0;
    virtual bool isConnected() const = 0;

    // Human-facing identity of the connected radio (model/nickname), for the
    // title bar and diagnostics.  Empty until connected.
    virtual QString displayName() const = 0;

public slots:
    // Lifecycle.  Implementations create their sockets/timers in the worker
    // thread on first connect (do not touch sockets in the constructor).
    virtual void connectToRadio(const RadioInfo& info) = 0;
    virtual void disconnectFromRadio() = 0;

    // --- Control intents (RX-first v1 set) --------------------------------
    // Frequency in Hz; slice/receiver index selects which VFO/RX for
    // dual-receiver radios (0 = primary).  Backends that lack a concept map
    // sensibly or no-op with a logged warning.
    virtual void setFrequency(int receiver, quint64 hz) = 0;
    virtual void setMode(int receiver, const QString& mode) = 0;
    virtual void setFilterBandwidth(int receiver, int lowHz, int highHz) = 0;
    // Request a fresh push of current state (freq/mode/filter) from the radio.
    virtual void refreshState() = 0;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString& message);

    // Normalized radio state.  `object` is a logical object name (e.g.
    // "slice 0", "radio"), `kvs` its key/value fields.
    void statusReceived(const QString& object, const QMap<QString, QString>& kvs);

    // Format-agnostic RX media, ready for AudioEngine / PanadapterModel.
    void audioDataReady(const QByteArray& pcmFloatMono);
    void spectrumReady(quint32 streamId, const QVector<float>& binsDbm,
                       qint64 emittedNs);
    void waterfallRowReady(quint32 streamId, const QVector<float>& binsDbm,
                           double lowFreqMhz, double highFreqMhz,
                           quint32 timecode, qint64 emittedNs);
};

} // namespace AetherSDR
