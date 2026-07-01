#include "SecretStore.h"

#include "LogManager.h"

namespace AetherSDR {

SecretStore& SecretStore::instance()
{
    static SecretStore s;
    return s;
}

SecretStore::SecretStore()
{
    // TODO(#12): when QtKeychain (Qt6Keychain) is added as an optional
    // dependency, probe it here and set m_keychainAvailable accordingly.
    // Until then no durable backend exists.
    m_keychainAvailable = false;
}

bool SecretStore::setSecret(const QString& key, const QString& secret)
{
    if (m_keychainAvailable) {
        // TODO(#12): write to the OS keychain via QtKeychain and return true.
    }
    m_sessionSecrets.insert(key, secret);
    qCInfo(lcIcom).noquote()
        << QStringLiteral("SecretStore: '%1' held in memory for this session "
                          "only (no keychain backend; see #3).")
               .arg(key);
    return false;  // not persisted durably
}

QString SecretStore::secret(const QString& key) const
{
    if (m_keychainAvailable) {
        // TODO(#12): read from the OS keychain via QtKeychain.
    }
    return m_sessionSecrets.value(key);
}

void SecretStore::removeSecret(const QString& key)
{
    if (m_keychainAvailable) {
        // TODO(#12): delete from the OS keychain via QtKeychain.
    }
    m_sessionSecrets.remove(key);
}

} // namespace AetherSDR
