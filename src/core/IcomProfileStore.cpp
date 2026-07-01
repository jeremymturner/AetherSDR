#include "IcomProfileStore.h"

#include "AppSettings.h"
#include "LogManager.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace AetherSDR {
namespace IcomProfileStore {

namespace {

// JSON field names.  PascalCase is reserved for AppSettings' top-level keys;
// the JSON object fields mirror the IcomConnectionProfile members directly.
constexpr const char* kFieldId = "id";
constexpr const char* kFieldDisplayName = "displayName";
constexpr const char* kFieldModelKey = "modelKey";
constexpr const char* kFieldAddress = "address";
constexpr const char* kFieldControlPort = "controlPort";
constexpr const char* kFieldUsername = "username";

QJsonObject profileToObject(const IcomConnectionProfile& profile)
{
    QJsonObject obj;
    obj.insert(QString::fromLatin1(kFieldId), profile.id);
    obj.insert(QString::fromLatin1(kFieldDisplayName), profile.displayName);
    obj.insert(QString::fromLatin1(kFieldModelKey), profile.modelKey);
    // QHostAddress persists as a string ("" for a null/unset address).
    obj.insert(QString::fromLatin1(kFieldAddress), profile.address.toString());
    obj.insert(QString::fromLatin1(kFieldControlPort),
               static_cast<int>(profile.controlPort));
    obj.insert(QString::fromLatin1(kFieldUsername), profile.username);
    return obj;
}

} // namespace

QJsonArray profilesToJson(const QVector<IcomConnectionProfile>& profiles)
{
    QJsonArray array;
    for (const IcomConnectionProfile& profile : profiles) {
        array.append(profileToObject(profile));
    }
    return array;
}

QVector<IcomConnectionProfile> profilesFromJson(const QJsonArray& array)
{
    QVector<IcomConnectionProfile> profiles;
    profiles.reserve(array.size());

    for (const QJsonValue& value : array) {
        if (!value.isObject()) {
            qCWarning(lcIcom)
                << "IcomProfileStore: skipping non-object entry in profile list";
            continue;
        }

        const QJsonObject obj = value.toObject();

        IcomConnectionProfile profile;
        profile.id = obj.value(QString::fromLatin1(kFieldId)).toString();

        // An id is the stable key (SecretStore + persistence).  Without it the
        // entry is unusable, so skip it rather than materialize a broken profile.
        if (profile.id.isEmpty()) {
            qCWarning(lcIcom)
                << "IcomProfileStore: skipping profile entry with missing/empty id";
            continue;
        }

        profile.displayName =
            obj.value(QString::fromLatin1(kFieldDisplayName)).toString();
        profile.modelKey = obj.value(QString::fromLatin1(kFieldModelKey)).toString();

        // QHostAddress parses the stored string; an invalid/empty string yields
        // a null address, which is a valid "not yet set" state.
        const QString addressText =
            obj.value(QString::fromLatin1(kFieldAddress)).toString();
        if (!addressText.isEmpty()) {
            QHostAddress address;
            if (address.setAddress(addressText)) {
                profile.address = address;
            } else {
                qCWarning(lcIcom).noquote()
                    << QStringLiteral("IcomProfileStore: profile '%1' has an unparseable "
                                      "address '%2'; leaving it unset.")
                           .arg(profile.id, addressText);
            }
        }

        // controlPort: default (kControlPort) unless a valid 1..65535 override
        // is present.  QJsonValue::toInt falls back to 0 for missing/non-number.
        const int port = obj.value(QString::fromLatin1(kFieldControlPort)).toInt(0);
        if (port > 0 && port <= 0xFFFF) {
            profile.controlPort = static_cast<quint16>(port);
        }

        profile.username = obj.value(QString::fromLatin1(kFieldUsername)).toString();

        profiles.append(profile);
    }

    return profiles;
}

QVector<IcomConnectionProfile> profilesFromJsonText(const QString& jsonText)
{
    if (jsonText.isEmpty()) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument doc =
        QJsonDocument::fromJson(jsonText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qCWarning(lcIcom).noquote()
            << QStringLiteral("IcomProfileStore: stored profile list is not valid JSON "
                              "(%1); ignoring it.")
                   .arg(parseError.errorString());
        return {};
    }
    if (!doc.isArray()) {
        qCWarning(lcIcom)
            << "IcomProfileStore: stored profile list root is not a JSON array; ignoring it";
        return {};
    }

    return profilesFromJson(doc.array());
}

QVector<IcomConnectionProfile> load()
{
    const QString jsonText =
        AppSettings::instance().value(QString::fromLatin1(kRootKey)).toString();
    return profilesFromJsonText(jsonText);
}

void save(const QVector<IcomConnectionProfile>& profiles)
{
    const QJsonDocument doc(profilesToJson(profiles));
    const QString jsonText =
        QString::fromUtf8(doc.toJson(QJsonDocument::Compact));

    auto& settings = AppSettings::instance();
    settings.setValue(QString::fromLatin1(kRootKey), jsonText);
    settings.save();
}

void addOrUpdate(const IcomConnectionProfile& profile)
{
    if (profile.id.isEmpty()) {
        qCWarning(lcIcom)
            << "IcomProfileStore: refusing to store a profile with an empty id";
        return;
    }

    QVector<IcomConnectionProfile> profiles = load();
    bool replaced = false;
    for (IcomConnectionProfile& existing : profiles) {
        if (existing.id == profile.id) {
            existing = profile;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        profiles.append(profile);
    }
    save(profiles);
}

void remove(const QString& id)
{
    if (id.isEmpty()) {
        return;
    }

    QVector<IcomConnectionProfile> profiles = load();
    const qsizetype before = profiles.size();
    profiles.removeIf([&id](const IcomConnectionProfile& profile) {
        return profile.id == id;
    });
    if (profiles.size() != before) {
        save(profiles);
    }
}

} // namespace IcomProfileStore
} // namespace AetherSDR
