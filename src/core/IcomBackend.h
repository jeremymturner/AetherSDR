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

private:
    void handleFrequency(quint64 hz);
    void handleMode(IcomCiv::CivMode mode, int filter);
    void handleScope(const QByteArray& civ27Payload);

    // Single receiver for v1; dual-RX (7610/9700) maps receiver->streamId later.
    static constexpr quint32 kScopeStreamId = 0;

    IcomUdpTransport* m_transport{nullptr};
    IcomConnectionProfile m_profile;
    quint8 m_civAddr{0};

    IcomScope::ScopeAssembler m_scope;
    float m_scopeRefDbm{IcomScope::kProvisionalDefaultRefDbm};
    float m_scopeRangeDb{IcomScope::kProvisionalDefaultRangeDb};

    // Negotiated audio format.  PROVISIONAL default; the real value comes from
    // the transport's audio-session negotiation (#3/#7).
    IcomAudio::IcomAudioFormat m_audioFormat{IcomAudio::IcomAudioFormat::ULaw8};

    bool m_connected{false};
};

} // namespace AetherSDR
