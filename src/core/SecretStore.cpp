#include "SecretStore.h"

#include "LogManager.h"

#ifdef HAVE_KEYCHAIN
#include <qt6keychain/keychain.h>

#include <QEventLoop>
#endif

namespace AetherSDR {

#ifdef HAVE_KEYCHAIN
// Keychain service string for all Icom radio-login secrets.  Kept distinct from
// SmartLinkClient's "AetherSDR" service so the two credential domains do not
// collide within the OS keychain.
static const QString kKeychainService = QStringLiteral("AetherSDR-Icom");
#endif

SecretStore& SecretStore::instance()
{
    static SecretStore s;
    return s;
}

SecretStore::SecretStore()
{
#ifdef HAVE_KEYCHAIN
    // QtKeychain is linked (same dependency SmartLinkClient uses).  Secrets are
    // backed by the OS keychain and durable across runs.
    m_keychainAvailable = true;
    qCInfo(lcIcom) << "SecretStore: durable credential persistence enabled via QtKeychain";
#else
    // No keychain backend: secrets live in memory for this session only.
    m_keychainAvailable = false;
    qCWarning(lcIcom)
        << "SecretStore: credential persistence disabled; Qt6Keychain not available at build "
           "time — Icom passwords held in memory for this session only";
#endif
}

bool SecretStore::setSecret(const QString& key, const QString& secret)
{
#ifdef HAVE_KEYCHAIN
    if (m_keychainAvailable) {
        QKeychain::WritePasswordJob job(kKeychainService);
        job.setAutoDelete(false);
        job.setKey(key);
        job.setTextData(secret);

        QEventLoop loop;
        QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        if (job.error() == QKeychain::NoError) {
            return true;  // persisted durably
        }

        qCWarning(lcIcom).noquote()
            << QStringLiteral("SecretStore: keychain write for '%1' failed (%2); falling back to "
                              "in-memory session store.")
                   .arg(key, job.errorString());
    }
#endif
    m_sessionSecrets.insert(key, secret);
    qCInfo(lcIcom).noquote()
        << QStringLiteral("SecretStore: '%1' held in memory for this session "
                          "only (no keychain backend; see #3).")
               .arg(key);
    return false;  // not persisted durably
}

QString SecretStore::secret(const QString& key) const
{
#ifdef HAVE_KEYCHAIN
    if (m_keychainAvailable) {
        QKeychain::ReadPasswordJob job(kKeychainService);
        job.setAutoDelete(false);
        job.setKey(key);

        QEventLoop loop;
        QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        if (job.error() == QKeychain::NoError) {
            return job.textData();
        }

        if (job.error() != QKeychain::EntryNotFound) {
            qCWarning(lcIcom).noquote()
                << QStringLiteral("SecretStore: keychain read for '%1' failed (%2); falling back to "
                                  "in-memory session store.")
                       .arg(key, job.errorString());
        }
        // EntryNotFound (or any error) falls through to the in-memory lookup,
        // which returns an empty string when the key is unknown.
    }
#endif
    return m_sessionSecrets.value(key);
}

void SecretStore::removeSecret(const QString& key)
{
#ifdef HAVE_KEYCHAIN
    if (m_keychainAvailable) {
        QKeychain::DeletePasswordJob job(kKeychainService);
        job.setAutoDelete(false);
        job.setKey(key);

        QEventLoop loop;
        QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
        job.start();
        loop.exec();

        if (job.error() != QKeychain::NoError && job.error() != QKeychain::EntryNotFound) {
            qCWarning(lcIcom).noquote()
                << QStringLiteral("SecretStore: keychain delete for '%1' failed (%2).")
                       .arg(key, job.errorString());
        }
    }
#endif
    // Also clear any in-memory copy (e.g. one written by a prior keychain-write
    // failure) so removeSecret is authoritative regardless of backend.
    m_sessionSecrets.remove(key);
}

} // namespace AetherSDR
