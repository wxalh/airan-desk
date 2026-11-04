#include "device_vault.h"
#include "device_vault_credentials.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <openssl/aead.h>
#include <openssl/evp.h>
#include <openssl/mem.h>
#include <openssl/rand.h>

#include <memory>

namespace
{
constexpr int kIterations = 600000;
constexpr int kMaxFileBytes = 4 * 1024 * 1024;
constexpr int kMaxEntries = 1000;
const QByteArray kAssociatedData = QByteArrayLiteral("airan-desk/my-devices/v1/AES-256-GCM/PBKDF2-SHA256/600000");

QString message(const char *text)
{
    return QCoreApplication::translate("DeviceVault", text);
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty())
        OPENSSL_cleanse(bytes.data(), size_t(bytes.size()));
    bytes.clear();
}

void wipeEntries(QVector<DeviceEntry> &entries)
{
    for (DeviceEntry &entry : entries)
    {
        entry.password.fill(QChar(0));
        entry.password.clear();
    }
    entries.clear();
}

bool randomBytes(int length, QByteArray *result)
{
    result->resize(length);
    return RAND_bytes(reinterpret_cast<uint8_t *>(result->data()), size_t(length)) == 1;
}

bool deriveKey(const QString &password, const QByteArray &salt, QByteArray *key)
{
    QByteArray encoded = password.toUtf8();
    key->resize(32);
    const bool success = PKCS5_PBKDF2_HMAC(
        encoded.constData(), size_t(encoded.size()),
        reinterpret_cast<const uint8_t *>(salt.constData()), size_t(salt.size()),
        kIterations, EVP_sha256(), size_t(key->size()), reinterpret_cast<uint8_t *>(key->data())) == 1;
    wipe(encoded);
    if (!success)
        wipe(*key);
    return success;
}

bool validPassword(const QString &password)
{
    return !password.isEmpty() && password.size() <= 1024;
}

bool validEntries(const QVector<DeviceEntry> &entries)
{
    if (entries.size() > kMaxEntries)
        return false;
    QSet<QString> ids;
    for (const DeviceEntry &entry : entries)
    {
        const QString id = entry.id.trimmed().toUpper();
        if (id.isEmpty() || id.size() > 256 || entry.id != entry.id.trimmed() ||
            entry.password.size() > 1024 || entry.note.size() > 4096 ||
            entry.group.size() > 256 || entry.os.size() > 256 || ids.contains(id))
            return false;
        ids.insert(id);
    }
    return true;
}

QByteArray serializeEntries(const QVector<DeviceEntry> &entries)
{
    QJsonArray rows;
    for (const DeviceEntry &entry : entries)
    {
        rows.append(QJsonObject{{QStringLiteral("id"), entry.id},
                                {QStringLiteral("password"), entry.password},
                                {QStringLiteral("note"), entry.note},
                                {QStringLiteral("group"), entry.group},
                                {QStringLiteral("os"), entry.os}});
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("devices"), rows}}).toJson(QJsonDocument::Compact);
}

bool parseEntries(const QByteArray &plain, QVector<DeviceEntry> *entries)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(plain, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        !document.object().value(QStringLiteral("devices")).isArray())
        return false;
    const QJsonArray rows = document.object().value(QStringLiteral("devices")).toArray();
    if (rows.size() > kMaxEntries)
        return false;
    QVector<DeviceEntry> parsed;
    for (const QJsonValue &row : rows)
    {
        if (!row.isObject())
            return false;
        const QJsonObject object = row.toObject();
        for (const QString &key : {QStringLiteral("id"), QStringLiteral("password"),
                                  QStringLiteral("note"), QStringLiteral("group"), QStringLiteral("os")})
        {
            if (!object.value(key).isString())
                return false;
        }
        parsed.append(DeviceEntry{object.value(QStringLiteral("id")).toString(),
                                  object.value(QStringLiteral("password")).toString(),
                                  object.value(QStringLiteral("note")).toString(),
                                  object.value(QStringLiteral("group")).toString(),
                                  object.value(QStringLiteral("os")).toString()});
    }
    if (!validEntries(parsed))
    {
        wipeEntries(parsed);
        return false;
    }
    *entries = parsed;
    return true;
}

bool decode(const QJsonObject &object, const QString &name, QByteArray *value)
{
    const QJsonValue field = object.value(name);
    if (!field.isString())
        return false;
    const QByteArray encoded = field.toString().toLatin1();
    *value = QByteArray::fromBase64(encoded);
    return value->toBase64() == encoded;
}

struct Envelope
{
    QByteArray salt;
    QByteArray nonce;
    QByteArray ciphertext;
    QByteArray revision;
};

bool readEnvelope(const QString &path, Envelope *envelope, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file could not be opened."));
        return false;
    }
    if (file.size() <= 0 || file.size() > kMaxFileBytes)
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file has an invalid size."));
        return false;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError jsonError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &jsonError);
    const QJsonObject object = document.object();
    if (jsonError.error != QJsonParseError::NoError || !document.isObject() ||
        object.value(QStringLiteral("version")).toInt() != 1 ||
        object.value(QStringLiteral("cipher")).toString() != QStringLiteral("AES-256-GCM") ||
        object.value(QStringLiteral("kdf")).toString() != QStringLiteral("PBKDF2-SHA256") ||
        object.value(QStringLiteral("iterations")).toInt() != kIterations ||
        !decode(object, QStringLiteral("salt"), &envelope->salt) || envelope->salt.size() != 16 ||
        !decode(object, QStringLiteral("nonce"), &envelope->nonce) || envelope->nonce.size() != 12 ||
        !decode(object, QStringLiteral("data"), &envelope->ciphertext) || envelope->ciphertext.size() < 16)
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file is invalid or uses an unsupported format."));
        return false;
    }
    envelope->revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return true;
}

using AeadContext = std::unique_ptr<EVP_AEAD_CTX, decltype(&EVP_AEAD_CTX_free)>;

AeadContext context(const QByteArray &key)
{
    if (key.size() != 32)
        return AeadContext(nullptr, EVP_AEAD_CTX_free);
    return AeadContext(EVP_AEAD_CTX_new(EVP_aead_aes_256_gcm(),
        reinterpret_cast<const uint8_t *>(key.constData()), size_t(key.size()), 16), EVP_AEAD_CTX_free);
}

bool decrypt(const Envelope &envelope, const QByteArray &key, QVector<DeviceEntry> *entries, QString *error)
{
    const auto aead = context(key);
    QByteArray plain(envelope.ciphertext.size(), '\0');
    const QByteArray associatedData = kAssociatedData + envelope.salt;
    size_t size = 0;
    const bool success = aead && EVP_AEAD_CTX_open(aead.get(),
        reinterpret_cast<uint8_t *>(plain.data()), &size, size_t(plain.size()),
        reinterpret_cast<const uint8_t *>(envelope.nonce.constData()), size_t(envelope.nonce.size()),
        reinterpret_cast<const uint8_t *>(envelope.ciphertext.constData()), size_t(envelope.ciphertext.size()),
        reinterpret_cast<const uint8_t *>(associatedData.constData()), size_t(associatedData.size())) == 1;
    if (!success)
    {
        wipe(plain);
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The device management password is incorrect, or the local device file has been damaged."));
        return false;
    }
    plain.resize(int(size));
    const bool parsed = parseEntries(plain, entries);
    wipe(plain);
    if (!parsed)
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The decrypted device data is invalid."));
    return parsed;
}

bool encrypt(const QByteArray &key, const QByteArray &salt, const QVector<DeviceEntry> &entries,
             QByteArray *bytes, QString *error)
{
    const auto aead = context(key);
    QByteArray nonce;
    QByteArray plain = serializeEntries(entries);
    const QByteArray associatedData = kAssociatedData + salt;
    QByteArray ciphertext(plain.size() + 16, '\0');
    size_t size = 0;
    const bool success = aead && randomBytes(12, &nonce) && EVP_AEAD_CTX_seal(aead.get(),
        reinterpret_cast<uint8_t *>(ciphertext.data()), &size, size_t(ciphertext.size()),
        reinterpret_cast<const uint8_t *>(nonce.constData()), size_t(nonce.size()),
        reinterpret_cast<const uint8_t *>(plain.constData()), size_t(plain.size()),
        reinterpret_cast<const uint8_t *>(associatedData.constData()), size_t(associatedData.size())) == 1;
    wipe(plain);
    if (!success)
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The device data could not be encrypted."));
        return false;
    }
    ciphertext.resize(int(size));
    const QJsonObject object{{QStringLiteral("version"), 1},
                             {QStringLiteral("cipher"), QStringLiteral("AES-256-GCM")},
                             {QStringLiteral("kdf"), QStringLiteral("PBKDF2-SHA256")},
                             {QStringLiteral("iterations"), kIterations},
                             {QStringLiteral("salt"), QString::fromLatin1(salt.toBase64())},
                             {QStringLiteral("nonce"), QString::fromLatin1(nonce.toBase64())},
                             {QStringLiteral("data"), QString::fromLatin1(ciphertext.toBase64())}};
    *bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes->size() > kMaxFileBytes)
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file would exceed the size limit."));
        return false;
    }
    return true;
}

bool commit(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file could not be saved. Your previous data has been preserved."));
        return false;
    }
    return true;
}

bool lockFile(QLockFile &lock, QString *error)
{
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(1500))
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device file is being used by another process. Please retry."));
        return false;
    }
    return true;
}

bool unchanged(const QString &path, const QByteArray &revision, QString *error)
{
    Envelope current;
    if (!readEnvelope(path, &current, error))
        return false;
    if (current.revision != revision)
    {
        *error = message(QT_TRANSLATE_NOOP("DeviceVault", "The device list was changed by another window or process. Close and reopen My Devices before saving."));
        return false;
    }
    return true;
}
}

DeviceVault::DeviceVault(const QString &path)
    : m_path(QFileInfo(path.isEmpty()
                          ? QDir::home().filePath(QStringLiteral(".wxalh/airan-desk/conf/my_devices.vault"))
                          : path).absoluteFilePath())
{
}

DeviceVault::~DeviceVault()
{
    lock();
}

bool DeviceVault::exists() const { return QFileInfo::exists(m_path); }
bool DeviceVault::isUnlocked() const { return m_key.size() == 32; }
bool DeviceVault::isRemembered() const { return DeviceVaultCredentials::exists(m_path); }
const QVector<DeviceEntry> &DeviceVault::entries() const { return m_entries; }
QString DeviceVault::errorString() const { return m_error; }
QString DeviceVault::path() const { return m_path; }

void DeviceVault::lock()
{
    wipe(m_key);
    wipe(m_salt);
    m_revision.clear();
    wipeEntries(m_entries);
}

bool DeviceVault::create(const QString &password, bool remember)
{
    m_error.clear();
    if (!validPassword(password))
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "Enter a non-empty device management password of at most 1024 characters."));
        return false;
    }
    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "The local device directory could not be created."));
        return false;
    }
    QLockFile fileLock(m_path + QStringLiteral(".lock"));
    if (!lockFile(fileLock, &m_error))
        return false;
    if (exists())
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "A local device file already exists. Unlock it to continue."));
        return false;
    }
    QByteArray salt;
    QByteArray key;
    QByteArray bytes;
    if (!randomBytes(16, &salt) || !deriveKey(password, salt, &key) || !encrypt(key, salt, {}, &bytes, &m_error))
    {
        wipe(key);
        if (m_error.isEmpty())
            m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "The device encryption key could not be created."));
        return false;
    }
    if (remember && !DeviceVaultCredentials::store(m_path, key, &m_error))
    {
        wipe(key);
        return false;
    }
    if (!commit(m_path, bytes, &m_error))
    {
        QString ignored;
        if (remember)
            DeviceVaultCredentials::remove(m_path, &ignored);
        wipe(key);
        return false;
    }
    // Remove any orphaned remembered key from a previously removed vault.
    if (!remember && isRemembered())
    {
        QString ignored;
        DeviceVaultCredentials::remove(m_path, &ignored);
    }
    lock();
    m_key = key;
    wipe(key);
    m_salt = salt;
    m_revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return true;
}

bool DeviceVault::unlock(const QString &password)
{
    m_error.clear();
    Envelope envelope;
    if (!readEnvelope(m_path, &envelope, &m_error))
        return false;
    QByteArray key;
    QVector<DeviceEntry> loaded;
    if (!validPassword(password) || !deriveKey(password, envelope.salt, &key) ||
        !decrypt(envelope, key, &loaded, &m_error))
    {
        wipe(key);
        if (m_error.isEmpty())
            m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "The device management password is incorrect."));
        return false;
    }
    lock();
    m_key = key;
    wipe(key);
    m_salt = envelope.salt;
    m_revision = envelope.revision;
    m_entries = loaded;
    return true;
}

bool DeviceVault::unlockRemembered()
{
    m_error.clear();
    Envelope envelope;
    QByteArray rememberedKeys;
    QByteArray key;
    QVector<DeviceEntry> loaded;
    if (!readEnvelope(m_path, &envelope, &m_error) ||
        !DeviceVaultCredentials::load(m_path, &rememberedKeys, &m_error))
    {
        wipe(rememberedKeys);
        return false;
    }
    for (int offset = 0; offset < rememberedKeys.size(); offset += 32)
    {
        QByteArray candidate = rememberedKeys.mid(offset, 32);
        const bool matched = decrypt(envelope, candidate, &loaded, &m_error);
        if (matched)
            key = candidate;
        wipe(candidate);
        if (matched)
            break;
    }
    wipe(rememberedKeys);
    if (key.isEmpty())
        return false;
    lock();
    m_key = key;
    wipe(key);
    m_salt = envelope.salt;
    m_revision = envelope.revision;
    m_entries = loaded;
    m_error.clear();
    return true;
}

bool DeviceVault::setRemembered(bool remember)
{
    m_error.clear();
    if (!remember && !isRemembered())
        return true;
    if (!isUnlocked())
    {
        if (remember)
        {
            m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "Unlock My Devices before remembering the password."));
            return false;
        }
    }
    QLockFile fileLock(m_path + QStringLiteral(".lock"));
    if (!lockFile(fileLock, &m_error))
        return false;
    if (!remember)
        return DeviceVaultCredentials::remove(m_path, &m_error);
    if (!unchanged(m_path, m_revision, &m_error))
        return false;
    return DeviceVaultCredentials::store(m_path, m_key, &m_error);
}

bool DeviceVault::save(const QVector<DeviceEntry> &entries)
{
    m_error.clear();
    if (!isUnlocked())
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "Unlock My Devices before saving changes."));
        return false;
    }
    if (!validEntries(entries))
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "Device data is invalid, too large, or contains duplicate device IDs."));
        return false;
    }
    QLockFile fileLock(m_path + QStringLiteral(".lock"));
    if (!lockFile(fileLock, &m_error) || !unchanged(m_path, m_revision, &m_error))
        return false;
    QByteArray bytes;
    if (!encrypt(m_key, m_salt, entries, &bytes, &m_error) || !commit(m_path, bytes, &m_error))
        return false;
    // Callers may pass entries() itself; copy before wiping the previous set.
    const QVector<DeviceEntry> updated = entries;
    wipeEntries(m_entries);
    m_entries = updated;
    m_revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    return true;
}

bool DeviceVault::changePassword(const QString &oldPassword, const QString &newPassword)
{
    m_error.clear();
    if (!isUnlocked() || !validPassword(newPassword))
    {
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "Unlock My Devices and enter a non-empty new management password."));
        return false;
    }
    QLockFile fileLock(m_path + QStringLiteral(".lock"));
    if (!lockFile(fileLock, &m_error) || !unchanged(m_path, m_revision, &m_error))
        return false;
    QByteArray oldKey;
    if (!validPassword(oldPassword) || !deriveKey(oldPassword, m_salt, &oldKey) ||
        oldKey.size() != m_key.size() || CRYPTO_memcmp(oldKey.constData(), m_key.constData(), size_t(m_key.size())) != 0)
    {
        wipe(oldKey);
        m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "The current device management password is incorrect."));
        return false;
    }
    wipe(oldKey);
    QByteArray salt;
    QByteArray key;
    QByteArray bytes;
    if (!randomBytes(16, &salt) || !deriveKey(newPassword, salt, &key) ||
        !encrypt(key, salt, m_entries, &bytes, &m_error))
    {
        wipe(key);
        if (m_error.isEmpty())
            m_error = message(QT_TRANSLATE_NOOP("DeviceVault", "The new device encryption key could not be created."));
        return false;
    }
    const bool remembered = isRemembered();
    QByteArray rotationKeys;
    if (remembered)
        rotationKeys = key + m_key;
    const bool credentialsReady = !remembered || DeviceVaultCredentials::store(m_path, rotationKeys, &m_error);
    wipe(rotationKeys);
    if (!credentialsReady)
    {
        QString ignored;
        DeviceVaultCredentials::store(m_path, m_key, &ignored);
        wipe(key);
        return false;
    }
    if (!commit(m_path, bytes, &m_error))
    {
        QString ignored;
        if (remembered)
            DeviceVaultCredentials::store(m_path, m_key, &ignored);
        wipe(key);
        return false;
    }
    wipe(m_key);
    m_key = key;
    wipe(key);
    m_salt = salt;
    m_revision = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    if (remembered)
    {
        // The two-key record already unlocks the committed vault. Cleanup is
        // optional: failure here must not turn a successful rotation into an
        // apparent failure, or discard the still-usable previous key record.
        QString ignored;
        DeviceVaultCredentials::store(m_path, m_key, &ignored);
    }
    return true;
}
