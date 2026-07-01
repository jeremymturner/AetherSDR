#include "IcomConnectDialog.h"

#include "core/IcomProfileStore.h"
#include "core/IcomRadioCapabilities.h"
#include "core/SecretStore.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUuid>
#include <QVBoxLayout>

namespace AetherSDR {

IcomConnectDialog::IcomConnectDialog(QWidget* parent)
    : PersistentDialog(tr("Connect to Icom Radio"),
                       QStringLiteral("IcomConnectDialogGeometry"), parent)
{
    resize(620, 380);
    buildUi();
    reloadProfiles();
    clearForm();
}

void IcomConnectDialog::buildUi()
{
    auto* outer = new QHBoxLayout(bodyWidget());

    // --- Left: saved profiles list ---------------------------------------
    auto* leftCol = new QVBoxLayout();
    auto* listLabel = new QLabel(tr("Saved radios"), bodyWidget());
    m_list = new QListWidget(bodyWidget());
    m_list->setAccessibleName(tr("Saved Icom radios"));
    m_list->setAccessibleDescription(
        tr("List of saved Icom connection profiles. Select one to edit or connect."));
    m_list->setMinimumWidth(180);
    connect(m_list, &QListWidget::currentRowChanged,
            this, [this](int) { onProfileSelected(); });
    leftCol->addWidget(listLabel);
    leftCol->addWidget(m_list, 1);
    m_newBtn = new QPushButton(tr("New"), bodyWidget());
    m_newBtn->setAccessibleName(tr("New Icom profile"));
    connect(m_newBtn, &QPushButton::clicked, this, &IcomConnectDialog::onNew);
    leftCol->addWidget(m_newBtn);
    outer->addLayout(leftCol);

    // --- Right: connection form ------------------------------------------
    auto* form = new QFormLayout();

    m_name = new QLineEdit(bodyWidget());
    m_name->setAccessibleName(tr("Profile name"));
    m_name->setPlaceholderText(tr("e.g. Shack IC-7610"));
    form->addRow(tr("Name"), m_name);

    m_model = new QComboBox(bodyWidget());
    m_model->setAccessibleName(tr("Icom model"));
    for (const IcomModelCaps& caps : allIcomModels()) {
        m_model->addItem(caps.displayName, caps.modelKey);
    }
    form->addRow(tr("Model"), m_model);

    m_host = new QLineEdit(bodyWidget());
    m_host->setAccessibleName(tr("Radio IP address"));
    m_host->setPlaceholderText(tr("e.g. 192.168.1.50"));
    form->addRow(tr("IP address"), m_host);

    m_port = new QSpinBox(bodyWidget());
    m_port->setRange(1, 65535);
    m_port->setValue(IcomUdpTransport::kControlPort);  // 50001
    m_port->setAccessibleName(tr("Control port"));
    form->addRow(tr("Control port"), m_port);

    m_user = new QLineEdit(bodyWidget());
    m_user->setAccessibleName(tr("Network username"));
    form->addRow(tr("Username"), m_user);

    m_pass = new QLineEdit(bodyWidget());
    m_pass->setEchoMode(QLineEdit::Password);
    m_pass->setAccessibleName(tr("Network password"));
    form->addRow(tr("Password"), m_pass);

    auto* keychainNote = new QLabel(
        SecretStore::instance().usingKeychain()
            ? tr("Password stored in the system keychain.")
            : tr("No keychain available — password kept for this session only."),
        bodyWidget());
    keychainNote->setWordWrap(true);
    form->addRow(QString(), keychainNote);

    auto* rightCol = new QVBoxLayout();
    rightCol->addLayout(form);
    rightCol->addStretch(1);

    // --- Action buttons ---------------------------------------------------
    auto* buttons = new QHBoxLayout();
    m_deleteBtn = new QPushButton(tr("Delete"), bodyWidget());
    m_deleteBtn->setAccessibleName(tr("Delete profile"));
    connect(m_deleteBtn, &QPushButton::clicked, this, &IcomConnectDialog::onDelete);
    m_saveBtn = new QPushButton(tr("Save"), bodyWidget());
    m_saveBtn->setAccessibleName(tr("Save profile"));
    connect(m_saveBtn, &QPushButton::clicked, this, &IcomConnectDialog::onSave);
    m_connectBtn = new QPushButton(tr("Connect"), bodyWidget());
    m_connectBtn->setAccessibleName(tr("Connect to this Icom radio"));
    m_connectBtn->setDefault(true);
    connect(m_connectBtn, &QPushButton::clicked, this, &IcomConnectDialog::onConnect);
    buttons->addWidget(m_deleteBtn);
    buttons->addStretch(1);
    buttons->addWidget(m_saveBtn);
    buttons->addWidget(m_connectBtn);
    rightCol->addLayout(buttons);

    outer->addLayout(rightCol, 1);
}

void IcomConnectDialog::reloadProfiles()
{
    m_profiles = IcomProfileStore::load();
    m_list->clear();
    for (const IcomConnectionProfile& p : m_profiles) {
        const QString label = p.displayName.isEmpty()
            ? QStringLiteral("%1 @ %2").arg(p.modelKey, p.address.toString())
            : p.displayName;
        m_list->addItem(label);
    }
}

void IcomConnectDialog::onProfileSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_profiles.size()) {
        return;
    }
    const IcomConnectionProfile& p = m_profiles.at(row);
    fillForm(p, SecretStore::instance().secret(p.id));
}

void IcomConnectDialog::fillForm(const IcomConnectionProfile& profile,
                                 const QString& password)
{
    m_currentId = profile.id;
    m_name->setText(profile.displayName);
    const int idx = m_model->findData(profile.modelKey);
    m_model->setCurrentIndex(idx >= 0 ? idx : 0);
    m_host->setText(profile.address.isNull() ? QString()
                                             : profile.address.toString());
    m_port->setValue(profile.controlPort != 0 ? profile.controlPort
                                              : IcomUdpTransport::kControlPort);
    m_user->setText(profile.username);
    m_pass->setText(password);
    m_deleteBtn->setEnabled(true);
}

void IcomConnectDialog::clearForm()
{
    m_currentId.clear();
    m_name->clear();
    m_model->setCurrentIndex(0);
    m_host->clear();
    m_port->setValue(IcomUdpTransport::kControlPort);
    m_user->clear();
    m_pass->clear();
    m_deleteBtn->setEnabled(false);
}

IcomConnectionProfile IcomConnectDialog::formToProfile() const
{
    IcomConnectionProfile p;
    p.id = m_currentId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : m_currentId;
    p.displayName = m_name->text().trimmed();
    p.modelKey = m_model->currentData().toString();
    p.address = QHostAddress(m_host->text().trimmed());
    p.controlPort = static_cast<quint16>(m_port->value());
    p.username = m_user->text().trimmed();
    return p;
}

bool IcomConnectDialog::validateForm(QString* error) const
{
    if (QHostAddress(m_host->text().trimmed()).isNull()) {
        if (error != nullptr) {
            *error = tr("Enter a valid IPv4/IPv6 address for the radio.");
        }
        return false;
    }
    return true;
}

bool IcomConnectDialog::persistCurrent(IcomConnectionProfile* saved)
{
    QString error;
    if (!validateForm(&error)) {
        QMessageBox::warning(this, tr("Icom connection"), error);
        return false;
    }
    IcomConnectionProfile p = formToProfile();
    // Save the secret first so a durable id exists before the profile lands.
    SecretStore::instance().setSecret(p.id, m_pass->text());
    IcomProfileStore::addOrUpdate(p);
    m_currentId = p.id;
    reloadProfiles();
    // Reselect the just-saved row so the list reflects the edit.
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id == p.id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
    if (saved != nullptr) {
        *saved = p;
    }
    return true;
}

void IcomConnectDialog::onNew()
{
    m_list->setCurrentRow(-1);
    clearForm();
    m_name->setFocus();
}

void IcomConnectDialog::onSave()
{
    persistCurrent(nullptr);
}

void IcomConnectDialog::onDelete()
{
    if (m_currentId.isEmpty()) {
        clearForm();
        return;
    }
    SecretStore::instance().removeSecret(m_currentId);
    IcomProfileStore::remove(m_currentId);
    reloadProfiles();
    clearForm();
}

void IcomConnectDialog::onConnect()
{
    IcomConnectionProfile saved;
    if (!persistCurrent(&saved)) {
        return;
    }
    emit connectRequested(saved);
    close();
}

} // namespace AetherSDR
