#pragma once

#include "core/IcomBackend.h"   // IcomConnectionProfile

#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace AetherSDR {

// Inline add/edit form for an Icom connection profile (#5).  Embedded in the
// ConnectionPanel's local page as a stacked sub-page — there is no separate
// Icom connect dialog; the unified "Connect to Radio" panel owns the flow.
// Persistence (IcomProfileStore + SecretStore) is handled by the host, which
// listens for saved().
class IcomProfileEditor : public QWidget {
    Q_OBJECT

public:
    explicit IcomProfileEditor(QWidget* parent = nullptr);

    // Populate for editing an existing profile (password from SecretStore), or
    // clear to a blank "new profile" form.
    void loadProfile(const IcomConnectionProfile& profile, const QString& password);
    void clearForm();
    // Start a new profile pre-filled with a detected radio's IP (#5 sweep):
    // the operator just picks the model and enters credentials.
    void prefillNewForAddress(const QString& ip);

    // Show (non-empty) or hide (empty) a highlighted banner above the form —
    // used to prompt the operator to add/fix credentials after a failed or
    // credential-less connect attempt (#5).  Cleared automatically whenever the
    // form is (re)loaded.
    void setNotice(const QString& text);

signals:
    // Emitted on Save with a valid form.  `profile.id` is stable (reused when
    // editing, freshly generated when new).  Password is carried separately so
    // the host can route it to SecretStore, never to disk.
    void saved(const IcomConnectionProfile& profile, const QString& password);
    void cancelled();

private:
    bool validate(QString* error) const;
    IcomConnectionProfile formToProfile() const;
    void onSaveClicked();

    QLabel*    m_notice{nullptr};
    QComboBox* m_model{nullptr};
    QLineEdit* m_name{nullptr};
    QLineEdit* m_host{nullptr};
    QSpinBox*  m_port{nullptr};
    QLineEdit* m_user{nullptr};
    QLineEdit* m_pass{nullptr};

    QString m_editingId;  // empty = new profile
};

} // namespace AetherSDR
