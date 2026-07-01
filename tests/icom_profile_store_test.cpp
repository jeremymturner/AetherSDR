#include "core/IcomProfileStore.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include <cstdio>

// Hermetic test for IcomProfileStore.  It exercises ONLY the pure
// serialize/deserialize helpers (profilesToJson / profilesFromJson /
// profilesFromJsonText), never load()/save(), so it does not touch the
// AppSettings singleton or write to ~/.config.

namespace {

int g_failures = 0;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "icom_profile_store_test: FAIL: %s\n", message);
        ++g_failures;
    }
}

} // namespace

int main()
{
    using namespace AetherSDR;

    // ── Round-trip: several profiles including edge cases ─────────────────────
    {
        QVector<IcomConnectionProfile> in;

        IcomConnectionProfile a;
        a.id = QStringLiteral("profile-a");
        a.displayName = QStringLiteral("Shack IC-7610");
        a.modelKey = QStringLiteral("IC-7610");
        a.address = QHostAddress(QStringLiteral("192.168.1.50"));
        a.controlPort = 50001;
        a.username = QStringLiteral("operator");
        in.append(a);

        // Edge case: null address, empty username, non-default port.
        IcomConnectionProfile b;
        b.id = QStringLiteral("profile-b");
        b.displayName = QStringLiteral("Portable IC-705");
        b.modelKey = QStringLiteral("IC-705");
        b.controlPort = 12345;
        in.append(b);

        // Edge case: unicode display name, IPv6 address.
        IcomConnectionProfile c;
        c.id = QStringLiteral("profile-c");
        c.displayName = QStringLiteral("Remote Übertragung");
        c.modelKey = QStringLiteral("IC-9700");
        c.address = QHostAddress(QStringLiteral("fe80::1"));
        c.username = QStringLiteral("guest");
        in.append(c);

        const QJsonArray json = IcomProfileStore::profilesToJson(in);
        const QVector<IcomConnectionProfile> out =
            IcomProfileStore::profilesFromJson(json);

        check(out.size() == in.size(), "round-trip size mismatch");
        if (out.size() == in.size()) {
            for (int i = 0; i < in.size(); ++i) {
                check(out[i].id == in[i].id, "round-trip id mismatch");
                check(out[i].displayName == in[i].displayName,
                      "round-trip displayName mismatch");
                check(out[i].modelKey == in[i].modelKey,
                      "round-trip modelKey mismatch");
                check(out[i].address == in[i].address,
                      "round-trip address mismatch");
                check(out[i].controlPort == in[i].controlPort,
                      "round-trip controlPort mismatch");
                check(out[i].username == in[i].username,
                      "round-trip username mismatch");
            }
        }

        // Confirm no password field is ever emitted (defence in depth: the
        // struct has none, but assert the JSON schema stays secret-free).
        for (const QJsonValue& value : json) {
            const QJsonObject obj = value.toObject();
            check(!obj.contains(QStringLiteral("password")),
                  "serialized JSON must not contain a password field");
            check(!obj.contains(QStringLiteral("secret")),
                  "serialized JSON must not contain a secret field");
        }
    }

    // ── Empty list round-trips to an empty array ──────────────────────────────
    {
        const QJsonArray json =
            IcomProfileStore::profilesToJson(QVector<IcomConnectionProfile>{});
        check(json.isEmpty(), "empty list should serialize to empty array");
        const QVector<IcomConnectionProfile> out =
            IcomProfileStore::profilesFromJson(json);
        check(out.isEmpty(), "empty array should deserialize to empty list");
    }

    // ── Malformed entries are skipped, valid ones survive ─────────────────────
    {
        QJsonArray array;

        // Not an object.
        array.append(QJsonValue(QStringLiteral("not-an-object")));
        array.append(QJsonValue(42));

        // Object missing the id field.
        QJsonObject noId;
        noId.insert(QStringLiteral("displayName"), QStringLiteral("orphan"));
        array.append(noId);

        // Object with empty id.
        QJsonObject emptyId;
        emptyId.insert(QStringLiteral("id"), QString());
        array.append(emptyId);

        // A valid object.
        QJsonObject good;
        good.insert(QStringLiteral("id"), QStringLiteral("keep-me"));
        good.insert(QStringLiteral("displayName"), QStringLiteral("Valid"));
        good.insert(QStringLiteral("modelKey"), QStringLiteral("IC-7300"));
        good.insert(QStringLiteral("address"), QStringLiteral("10.0.0.1"));
        good.insert(QStringLiteral("controlPort"), 50001);
        good.insert(QStringLiteral("username"), QStringLiteral("op"));
        array.append(good);

        const QVector<IcomConnectionProfile> out =
            IcomProfileStore::profilesFromJson(array);
        check(out.size() == 1, "only the one valid entry should survive");
        if (out.size() == 1) {
            check(out[0].id == QStringLiteral("keep-me"), "surviving entry id wrong");
            check(out[0].address == QHostAddress(QStringLiteral("10.0.0.1")),
                  "surviving entry address wrong");
        }
    }

    // ── Unparseable address is dropped, entry still kept ──────────────────────
    {
        QJsonArray array;
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), QStringLiteral("bad-addr"));
        obj.insert(QStringLiteral("address"), QStringLiteral("not-an-ip"));
        array.append(obj);

        const QVector<IcomConnectionProfile> out =
            IcomProfileStore::profilesFromJson(array);
        check(out.size() == 1, "entry with bad address should still be kept");
        if (out.size() == 1) {
            check(out[0].address.isNull(),
                  "unparseable address should leave the field null");
        }
    }

    // ── Malformed JSON text yields an empty list, not a crash ─────────────────
    {
        check(IcomProfileStore::profilesFromJsonText(QStringLiteral("")).isEmpty(),
              "empty text should yield empty list");
        check(IcomProfileStore::profilesFromJsonText(
                  QStringLiteral("{ this is not json"))
                  .isEmpty(),
              "garbage text should yield empty list");
        // Valid JSON but wrong root type (object, not array).
        check(IcomProfileStore::profilesFromJsonText(QStringLiteral("{\"a\":1}"))
                  .isEmpty(),
              "non-array root should yield empty list");
    }

    // ── Text round-trip through a compact JSON document ───────────────────────
    {
        QVector<IcomConnectionProfile> in;
        IcomConnectionProfile p;
        p.id = QStringLiteral("text-rt");
        p.displayName = QStringLiteral("Text Round Trip");
        p.modelKey = QStringLiteral("IC-7610");
        p.address = QHostAddress(QStringLiteral("172.16.5.4"));
        p.controlPort = 50001;
        p.username = QStringLiteral("u");
        in.append(p);

        const QJsonDocument doc(IcomProfileStore::profilesToJson(in));
        const QString text = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        const QVector<IcomConnectionProfile> out =
            IcomProfileStore::profilesFromJsonText(text);

        check(out.size() == 1, "text round-trip size mismatch");
        if (out.size() == 1) {
            check(out[0].id == in[0].id, "text round-trip id mismatch");
            check(out[0].address == in[0].address,
                  "text round-trip address mismatch");
            check(out[0].controlPort == in[0].controlPort,
                  "text round-trip controlPort mismatch");
        }
    }

    if (g_failures == 0) {
        std::printf("icom_profile_store_test: all checks passed\n");
        return 0;
    }
    std::fprintf(stderr, "icom_profile_store_test: %d check(s) failed\n", g_failures);
    return 1;
}
