#pragma once

#include <QHash>
#include <QString>

namespace AetherSDR {

// Storage for radio-login secrets (Icom network username passwords, #3/#5).
//
// Maintainer decision (epic #1): prefer the OS keychain when available
// (macOS Keychain / Windows Credential Store / libsecret / KWallet, via
// QtKeychain when built with HAVE_QTKEYCHAIN); otherwise fall back to
// encrypted AppSettings.
//
// INTERIM (this build): the keychain backend is a documented seam (QtKeychain
// is not yet a build dependency — tracked in #12) and the encrypted-AppSettings
// fallback is NOT implemented, because doing crypto without a vetted dependency
// would be worse than not persisting at all.  Until one of those lands, the
// fallback keeps secrets IN MEMORY for the session only and never writes a
// password to disk.  `usingKeychain()` reports the active backend so the
// connection UI can warn "password not remembered" when it is false.  Do NOT
// add a plaintext-to-disk fallback here — that is the trap this class exists to
// avoid (Constitution: boundary/secret handling).
class SecretStore {
public:
    static SecretStore& instance();

    // Store `secret` under `key` (use the connection-profile id as the key).
    // Returns true if it was persisted durably (keychain); false if it was
    // only held in memory for this session.
    bool setSecret(const QString& key, const QString& secret);

    // Retrieve a secret, or an empty string if none is known.
    QString secret(const QString& key) const;

    void removeSecret(const QString& key);

    // True when secrets are backed by the OS keychain (durable across runs).
    bool usingKeychain() const { return m_keychainAvailable; }

private:
    SecretStore();
    SecretStore(const SecretStore&) = delete;
    SecretStore& operator=(const SecretStore&) = delete;

    bool m_keychainAvailable{false};
    QHash<QString, QString> m_sessionSecrets;  // in-memory fallback only
};

} // namespace AetherSDR
