#pragma once

#include "IcomAudioCodec.h"
#include "IcomCivCodec.h"
#include "IcomScopeParser.h"
#include "IcomUdpTransport.h"
#include "RadioBackend.h"

#include <QHostAddress>
#include <QString>

#include <cstdint>

namespace AetherSDR {

// A saved/entered Icom network connection.  Password is NOT stored here; it is
// fetched from SecretStore by `id` at connect time (#3/#5).
struct IcomConnectionProfile {
    QString id;          // stable key (SecretStore + persistence)
    QString displayName; // human label in the connect UI
    QString modelKey;    // "IC-7610" etc. -> capabilities + CI-V address
    QHostAddress address;
    quint16 controlPort{IcomUdpTransport::kControlPort};
    QString username;
};

// RadioBackend implementation for network-capable Icom radios (issue #2 impl,
// epic #1).  Owns an IcomUdpTransport and converts the radio's native CI-V /
// scope / audio into the format-agnostic RadioBackend signals that RadioModel,
// AudioEngine and PanadapterModel already consume.
//
// RX-first (maintainer decision): frequency/mode control + scope + RX audio.
// TX is a deliberate fast-follow gated by transmit-on-intent (#9) and is not
// wired here.
//
// Threading: like RadioConnection (#502), construct then moveToThread() a
// worker.  IcomBackend parents its transport to itself so they move together.
class IcomBackend : public RadioBackend {
    Q_OBJECT

public:
    explicit IcomBackend(QObject* parent = nullptr);
    ~IcomBackend() override;

    RadioType type() const override { return RadioType::Icom; }
    bool isConnected() const override { return m_connected; }
    QString displayName() const override { return m_profile.displayName; }

    // Set the connection profile (host/model/username) before connecting.
    void setConnectionProfile(const IcomConnectionProfile& profile);

public slots:
    void connectToRadio(const RadioInfo& info) override;
    void disconnectFromRadio() override;
    void setFrequency(int receiver, quint64 hz) override;
    void setMode(int receiver, const QString& mode) override;
    void setFilterBandwidth(int receiver, int lowHz, int highHz) override;
    void refreshState() override;

private slots:
    void onTransportConnected();
    void onTransportDisconnected();
    void onCivFrameReceived(const QByteArray& civFrame);
    void onAudioPayloadReceived(const QByteArray& payload);

    // --- Pure, socket-free mapping helpers (unit-testable; see the test). ---
public:
    // Map a requested audio bandwidth (highHz - lowHz) to the nearest Icom
    // discrete filter slot (FIL1/FIL2/FIL3) for a given mode.  Icom does not
    // accept arbitrary Hz edges over CI-V, so a request is quantized to the
    // closest slot by that slot's PROVISIONAL nominal width (widths are
    // radio-configurable in the DSP menu — see the kProvisional* constants in
    // the .cpp).  Returns one of IcomCiv::kFilterFil1/2/3.
    static quint8 filterSlotForBandwidth(IcomCiv::CivMode mode, int bandwidthHz);

    // Map a RadioBackend receiver index (0 = main, 1 = sub) to the scope
    // stream id used by spectrum/waterfall signals.  For radios with a single
    // scope, everything collapses onto the main stream.  `scopeCount` comes
    // from icomCapsFor(modelKey).scopeCount.
    static quint32 scopeStreamIdForReceiver(int receiver, int scopeCount);

    // mode string <-> CivMode (exposed for tests; the wire layer stays in the
    // codec).  Unknown strings default to USB, mirroring the RX-first glue.
    static IcomCiv::CivMode civModeFromString(const QString& mode);
    static QString civModeToString(IcomCiv::CivMode mode);

private:
    void handleFrequency(quint64 hz);
    void handleMode(IcomCiv::CivMode mode, int filter);
    void handleScope(const QByteArray& civ27Payload);

    // Turn the spectrum scope + CI-V waveform output on (called on connect for
    // radios with a scope).  See enableScope() in the .cpp for the wire detail.
    void enableScope();

    // Select the target VFO/receiver on a dual-RX radio before a freq/mode
    // command.  No-ops (returns false) for single-RX models or receiver 0.
    bool selectReceiver(int receiver);

    // Scope stream ids: main panadapter on 0, sub-RX panadapter on 1 so two
    // panadapters can render independently.
    static constexpr quint32 kScopeStreamIdMain = 0;
    static constexpr quint32 kScopeStreamIdSub = 1;

    IcomUdpTransport* m_transport{nullptr};
    IcomConnectionProfile m_profile;
    quint8 m_civAddr{0};

    IcomScope::ScopeAssembler m_scope;
    float m_scopeRefDbm{IcomScope::kProvisionalDefaultRefDbm};
    float m_scopeRangeDb{IcomScope::kProvisionalDefaultRangeDb};

    // Negotiated audio format.  PROVISIONAL default; the real value comes from
    // the transport's audio-session negotiation (#3/#7).
    IcomAudio::IcomAudioFormat m_audioFormat{IcomAudio::IcomAudioFormat::PCM16};

    // Last known operating mode, kept so setFilterBandwidth can re-send
    // mode+filter (CI-V cmd 0x06 carries mode AND filter together) without
    // clobbering the mode.  Updated by setMode() and by decoded mode replies.
    IcomCiv::CivMode m_currentMode{IcomCiv::CivMode::Usb};

    bool m_connected{false};
};

} // namespace AetherSDR
