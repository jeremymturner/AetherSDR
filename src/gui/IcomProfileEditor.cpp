#include "IcomProfileEditor.h"

#include "core/IcomRadioCapabilities.h"
#include "core/SecretStore.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

namespace AetherSDR {

IcomProfileEditor::IcomProfileEditor(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel(tr("Add Icom radio"), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    root->addWidget(title);

    // Prompt banner (hidden until setNotice() is given text): surfaces the
    // "add credentials" / "connection failed — check credentials" guidance.
    m_notice = new QLabel(this);
    m_notice->setObjectName(QStringLiteral("icomEditorNotice"));
    m_notice->setAccessibleName(tr("Icom connection notice"));
    m_notice->setWordWrap(true);
    m_notice->setStyleSheet(QStringLiteral(
        "QLabel#icomEditorNotice { color: #ffd27f; background: #2a2113; "
        "border: 1px solid #6a5320; border-radius: 4px; padding: 6px 8px; }"));
    m_notice->setVisible(false);
    root->addWidget(m_notice);

    auto* form = new QFormLayout();

    m_name = new QLineEdit(this);
    m_name->setAccessibleName(tr("Profile name"));
    m_name->setPlaceholderText(tr("e.g. Shack IC-7610"));
    form->addRow(tr("Name"), m_name);

    m_model = new QComboBox(this);
    m_model->setAccessibleName(tr("Icom model"));
    for (const IcomModelCaps& caps : allIcomModels()) {
        m_model->addItem(caps.displayName, caps.modelKey);
    }
    form->addRow(tr("Model"), m_model);

    m_host = new QLineEdit(this);
    m_host->setAccessibleName(tr("Icom radio IP address"));
    m_host->setPlaceholderText(tr("e.g. 192.168.1.50"));
    form->addRow(tr("IP address"), m_host);

    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(IcomUdpTransport::kControlPort);
    m_port->setAccessibleName(tr("Control port"));
    form->addRow(tr("Control port"), m_port);

    m_user = new QLineEdit(this);
    m_user->setAccessibleName(tr("Network username"));
    form->addRow(tr("Username"), m_user);

    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setAccessibleName(tr("Network password"));
    form->addRow(tr("Password"), m_pass);

    auto* note = new QLabel(
        SecretStore::instance().usingKeychain()
            ? tr("Password stored in the system keychain.")
            : tr("No keychain available — password kept for this session only."),
        this);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    root->addLayout(form);
    root->addStretch(1);

    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setAccessibleName(tr("Cancel Icom profile edit"));
    connect(cancelBtn, &QPushButton::clicked, this, &IcomProfileEditor::cancelled);
    auto* saveBtn = new QPushButton(tr("Save"), this);
    saveBtn->setAccessibleName(tr("Save Icom profile"));
    saveBtn->setDefault(true);
    connect(saveBtn, &QPushButton::clicked, this, &IcomProfileEditor::onSaveClicked);
    buttons->addWidget(cancelBtn);
    buttons->addWidget(saveBtn);
    root->addLayout(buttons);
}

void IcomProfileEditor::setNotice(const QString& text)
{
    if (m_notice == nullptr) {
        return;
    }
    m_notice->setText(text);
    m_notice->setVisible(!text.trimmed().isEmpty());
}

void IcomProfileEditor::clearForm()
{
    setNotice(QString());
    m_editingId.clear();
    m_name->clear();
    m_model->setCurrentIndex(0);
    m_host->clear();
    m_port->setValue(IcomUdpTransport::kControlPort);
    m_user->clear();
    m_pass->clear();
}

void IcomProfileEditor::prefillNewForAddress(const QString& ip)
{
    clearForm();
    m_host->setText(ip);
    m_name->setFocus();
}

void IcomProfileEditor::loadProfile(const IcomConnectionProfile& profile,
                                    const QString& password)
{
    setNotice(QString());
    m_editingId = profile.id;
    m_name->setText(profile.displayName);
    const int idx = m_model->findData(profile.modelKey);
    m_model->setCurrentIndex(idx >= 0 ? idx : 0);
    m_host->setText(profile.address.isNull() ? QString()
                                             : profile.address.toString());
    m_port->setValue(profile.controlPort != 0 ? profile.controlPort
                                              : IcomUdpTransport::kControlPort);
    m_user->setText(profile.username);
    m_pass->setText(password);
}

bool IcomProfileEditor::validate(QString* error) const
{
    if (QHostAddress(m_host->text().trimmed()).isNull()) {
        if (error != nullptr) {
            *error = tr("Enter a valid IPv4/IPv6 address for the radio.");
        }
        return false;
    }
    return true;
}

IcomConnectionProfile IcomProfileEditor::formToProfile() const
{
    IcomConnectionProfile p;
    p.id = m_editingId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : m_editingId;
    p.displayName = m_name->text().trimmed();
    p.modelKey = m_model->currentData().toString();
    p.address = QHostAddress(m_host->text().trimmed());
    p.controlPort = static_cast<quint16>(m_port->value());
    p.username = m_user->text().trimmed();
    return p;
}

void IcomProfileEditor::onSaveClicked()
{
    QString error;
    if (!validate(&error)) {
        QMessageBox::warning(this, tr("Icom radio"), error);
        return;
    }
    emit saved(formToProfile(), m_pass->text());
}

} // namespace AetherSDR
