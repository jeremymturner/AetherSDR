#pragma once

#include "PersistentDialog.h"
#include "core/IcomBackend.h"   // IcomConnectionProfile

#include <QString>
#include <QVector>

class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace AetherSDR {

// "Connect to Icom Radio" dialog (#5).  Manages a list of saved Icom
// connection profiles (persisted via IcomProfileStore; passwords via
// SecretStore) and requests a connection through the RadioBackend seam.
//
// Non-modal / lazy / geometry-persistent / frameless-aware via PersistentDialog.
class IcomConnectDialog : public PersistentDialog {
    Q_OBJECT

public:
    explicit IcomConnectDialog(QWidget* parent = nullptr);

signals:
    // The operator asked to connect with this profile.  MainWindow routes it to
    // RadioModel::connectToIcom().  The password is already saved to SecretStore
    // under profile.id before this fires.
    void connectRequested(const IcomConnectionProfile& profile);

private:
    void buildUi();
    void reloadProfiles();
    void onProfileSelected();
    void fillForm(const IcomConnectionProfile& profile, const QString& password);
    void clearForm();
    IcomConnectionProfile formToProfile() const;
    bool validateForm(QString* error) const;
    bool persistCurrent(IcomConnectionProfile* saved);
    void onNew();
    void onSave();
    void onDelete();
    void onConnect();

    QListWidget* m_list{nullptr};
    QLineEdit*   m_name{nullptr};
    QComboBox*   m_model{nullptr};
    QLineEdit*   m_host{nullptr};
    QSpinBox*    m_port{nullptr};
    QLineEdit*   m_user{nullptr};
    QLineEdit*   m_pass{nullptr};
    QPushButton* m_newBtn{nullptr};
    QPushButton* m_saveBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};

    QVector<IcomConnectionProfile> m_profiles;
    QString m_currentId;  // id being edited; empty = unsaved new profile
};

} // namespace AetherSDR
