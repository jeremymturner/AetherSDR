#pragma once

#include "core/RadioDiscovery.h"
#include "core/SmartLinkClient.h"
#include "core/IcomBackend.h"   // IcomConnectionProfile (unified radio list, #5)

#include <QVector>
#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QStackedWidget>
#include <QToolButton>

class QVBoxLayout;

namespace AetherSDR {

class IcomProfileEditor;

// Novice-first dialog for local, SmartLink, and manual/VPN radio connections.
class ConnectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget* parent = nullptr);

    void setFramelessMode(bool on);
    void setConnected(bool connected);
    void setStatusText(const QString& text);
    void probeRadio(const QString& ip);
    QList<RadioInfo> automationLocalRadios() const;
    bool automationConnectLocalSerial(const QString& serial, QString* error = nullptr);
    bool automationConnectByIp(const QString& hostOrIp, QString* error = nullptr);
    bool automationDisconnect(QString* error = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* e) override;

public slots:
    void onRadioDiscovered(const RadioInfo& radio);
    void onRadioUpdated(const RadioInfo& radio);
    void onRadioLost(const QString& serial);

    // Active Icom LAN sweep results (#5): a detected radio (address only,
    // model unknown pre-auth) appears in the unified list; connecting to one
    // opens the editor pre-filled so the operator sets model + credentials.
    void onIcomRadioDiscovered(const QHostAddress& address);
    void onIcomRadioLost(const QHostAddress& address);

    // An in-progress Icom connection failed (bad/expired credentials, radio
    // unreachable, …).  Re-opens the editor for the profile we were connecting
    // to so the operator can fix the credentials and retry (#5).  Ignored when
    // no Icom connect was pending.
    void onIcomConnectFailed(const QString& error);

    // SmartLink
    void setSmartLinkClient(SmartLinkClient* client);

signals:
    void connectRequested(const RadioInfo& radio);
    // A saved Icom radio was chosen from the unified list (#5).  MainWindow
    // routes this to RadioModel::connectToIcom().
    void icomConnectRequested(const IcomConnectionProfile& profile);
    void wanConnectRequested(const WanRadioInfo& radio);
    void wanDisconnectClientsRequested(const WanRadioInfo& radio);
    void disconnectRequested();
    void routedRadioFound(const RadioInfo& radio);
    void retryDiscoveryRequested();
    void networkDiagnosticsRequested();
    void smartLinkLoginRequested(const QString& email, const QString& password);

private slots:
    void onConnectionModeClicked(int id);
    void onListSelectionChanged();
    void onWanSelectionChanged();
    void onLocalConnectClicked();
    void onWanConnectClicked();
    void onWanDisconnectClientsClicked();
    void onManualIpChanged(const QString& ip);
    void onManualConnectClicked();
    void onManualAdvancedToggled(bool checked);

private:
    enum ConnectionMode {
        LocalMode = 0,
        SmartLinkMode = 1,
        ManualMode = 2
    };

    void setCurrentMode(ConnectionMode mode);
    void updateLocalPageState();
    void updateSmartLinkUi();
    void updateActionState();
    void updateLowBandwidthVisibility();
    void updateManualAdvancedVisibility();
    void refreshManualSourceOptions(const RadioBindSettings* selected = nullptr);
    void applySavedSourceSelection(const QString& ip);
    RadioBindSettings currentManualBindSettings(bool* staleSelection = nullptr) const;
    void loadRecentManualIps();
    void rememberManualIp(const QString& ip);
    void saveManualProfile(const QString& targetIp,
                           const RadioBindSettings& settings,
                           const QHostAddress& lastSuccessfulLocalIp);
    void saveLowBandwidthPreference(bool enabled);
    void setManualMessage(const QString& text, bool error = false);
    QString formatLocalRadioLabel(const RadioInfo& radio) const;
    QString formatWanRadioLabel(const WanRadioInfo& radio) const;

    QWidget*     m_titleBar{nullptr};
    QVBoxLayout* m_rootLayout{nullptr};

    QButtonGroup* m_modeButtons{nullptr};
    QStackedWidget* m_modeStack{nullptr};
    QCommandLinkButton* m_localModeBtn{nullptr};
    QCommandLinkButton* m_smartLinkModeBtn{nullptr};
    QCommandLinkButton* m_manualModeBtn{nullptr};

    QLabel*      m_statusLabel;
    QPushButton* m_disconnectBtn{nullptr};

    QListWidget* m_radioList{nullptr};
    QStackedWidget* m_localStateStack{nullptr};
    QWidget* m_localEmptyState{nullptr};
    QPushButton* m_localConnectBtn{nullptr};

    QList<RadioInfo> m_radios;   // discovered Flex LAN radios
    bool m_connected{false};

    // --- Unified list: saved Icom radios alongside discovered Flex (#5) ----
    void reloadIcomProfiles();
    void rebuildRadioList();
    QString formatIcomRadioLabel(const IcomConnectionProfile& profile,
                                 bool detectedOnline) const;
    void showIcomEditor(const QString& profileId);  // empty id = new
    void onIcomEditorSaved(const IcomConnectionProfile& profile,
                           const QString& password);
    void deleteSelectedIcom();
    QString currentRowIcomId() const;  // empty when the current row is not Icom

    // Start connecting to a saved Icom profile — but if it has no stored
    // credentials, divert to the editor and prompt for them first (#5).
    void connectToSavedIcom(const IcomConnectionProfile& profile);
    bool icomAddressIsSaved(const QHostAddress& address) const;
    bool icomAddressIsDetected(const QHostAddress& address) const;
    // True when the profile has a username AND a stored password (keychain or
    // session), i.e. enough to attempt an authenticated connection.
    static bool icomProfileHasCredentials(const IcomConnectionProfile& profile);

    QVector<IcomConnectionProfile> m_icomProfiles;
    QVector<QHostAddress> m_icomDetected;  // swept, not-yet-saved Icom radios
    // Id of the profile a connect is in flight for (cleared on success/failure),
    // and a profile queued to auto-connect once its credentials are saved.
    QString m_connectingIcomId;
    QString m_pendingConnectIcomId;
    // Set when the editor was opened to set up a just-detected radio and the
    // operator's intent is to connect once credentials are saved (the profile
    // id does not exist until Save, so a bool rather than an id).
    bool m_connectAfterSaveNew{false};
    IcomProfileEditor* m_icomEditor{nullptr};
    QPushButton* m_addIcomBtn{nullptr};
    QPushButton* m_editIcomBtn{nullptr};
    QPushButton* m_removeIcomBtn{nullptr};

    // SmartLink UI
    SmartLinkClient* m_smartLink{nullptr};
    QWidget*     m_loginForm{nullptr};
    QLineEdit*   m_emailEdit{nullptr};
    QLineEdit*   m_passwordEdit{nullptr};
    QPushButton* m_loginBtn{nullptr};
    QPushButton* m_logoutBtn{nullptr};
    QLabel*      m_slUserLabel{nullptr};
    QListWidget* m_wanList{nullptr};
    QLabel*      m_smartLinkEmptyLabel{nullptr};
    QPushButton* m_wanDisconnectClientsBtn{nullptr};
    QPushButton* m_wanConnectBtn{nullptr};
    QList<WanRadioInfo> m_wanRadios;

    // Manual (VPN / routed) connection
    QComboBox*   m_manualIpCombo{nullptr};
    QLineEdit*   m_manualIpEdit{nullptr};
    QLabel*      m_manualResultLabel{nullptr};
    QToolButton* m_manualAdvancedToggle{nullptr};
    QWidget*     m_manualAdvancedWidget{nullptr};
    QComboBox*   m_manualSourceCombo{nullptr};
    QLabel*      m_manualSourceWarningLabel{nullptr};
    QPushButton* m_manualConnectBtn{nullptr};
    QString      m_manualProfileIp;
    bool         m_manualConnectPending{false};

    QCheckBox*   m_autoConnectCheck{nullptr};

    QWidget*     m_linkOptionsWidget{nullptr};
    QLabel*      m_lowBwHintLabel{nullptr};
    QCheckBox*   m_lowBwCheck{nullptr};
    QCheckBox*   m_adaptiveThrottleCheck{nullptr};
};

} // namespace AetherSDR
