#pragma once

#include "IcomBackend.h"  // IcomConnectionProfile

#include <QJsonArray>
#include <QString>
#include <QVector>

namespace AetherSDR {

// Persistence for the list of saved Icom connection profiles (#5).
//
// Constitution Principle V: this feature owns its configuration as a SINGLE
// self-contained object under ONE root key.  The whole profile list is stored
// as a JSON array under AppSettings["IcomProfiles"] — never as scattered flat
// keys.  Each element carries only non-secret fields:
//
//   {
//     "id":          "<stable key>",
//     "displayName": "<human label>",
//     "modelKey":    "IC-7610",
//     "address":     "192.168.1.50",   // QHostAddress serialized as a string
//     "controlPort": 50001,
//     "username":    "operator"
//   }
//
// The password is DELIBERATELY absent: it lives in SecretStore keyed by the
// profile id, so no plaintext credential is ever written to AppSettings/disk.
//
// The pure serialize/deserialize helpers (profilesToJson / profilesFromJson)
// are free functions with no dependency on the AppSettings singleton, so they
// can be exercised hermetically in tests.  load()/save()/addOrUpdate()/remove()
// wrap them around AppSettings for production use.
namespace IcomProfileStore {

// Root key under which the whole list persists (Principle V: one object).
inline constexpr const char* kRootKey = "IcomProfiles";

// ── Pure (AppSettings-independent) serialization helpers ──────────────────────

// Serialize a profile list to a JSON array (one object per profile, no secrets).
QJsonArray profilesToJson(const QVector<IcomConnectionProfile>& profiles);

// Deserialize a JSON array back into profiles.  Boundary-validated: elements
// that are not objects, or that lack a usable "id", are skipped rather than
// producing a malformed profile.  Never throws, never crashes.
QVector<IcomConnectionProfile> profilesFromJson(const QJsonArray& array);

// Parse a raw JSON document string (as stored in AppSettings) into profiles.
// A document that fails to parse, or whose root is not an array, yields an
// empty list.  Never throws, never crashes.
QVector<IcomConnectionProfile> profilesFromJsonText(const QString& jsonText);

// ── AppSettings-backed API ────────────────────────────────────────────────────

// Load the persisted profile list (empty if none/invalid).
QVector<IcomConnectionProfile> load();

// Persist the whole list as one object under kRootKey.
void save(const QVector<IcomConnectionProfile>& profiles);

// Insert `profile`, or replace the existing entry with the same id, then save.
void addOrUpdate(const IcomConnectionProfile& profile);

// Remove the profile with the given id (if present), then save.
void remove(const QString& id);

} // namespace IcomProfileStore

} // namespace AetherSDR
