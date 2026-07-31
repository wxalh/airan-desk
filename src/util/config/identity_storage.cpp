#include "identity_storage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSaveFile>
#include <QSettings>
#include <QTemporaryFile>
#include <QUuid>

namespace IdentityStorage
{
namespace
{
constexpr int kLockTimeoutMs = 1500;

void setError(QString *error, const QString &message)
{
    if (error)
        *error = message;
}

QString normalizedUuid(const QString &value)
{
    const QUuid uuid(value.trimmed());
    if (uuid.isNull())
        return QString();
    return uuid.toString().remove(QLatin1Char('{')).remove(QLatin1Char('}')).toUpper();
}

QString newUuid()
{
    return QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}')).toUpper();
}

bool validSnapshot(const Snapshot &snapshot)
{
    return !normalizedUuid(snapshot.localId).isEmpty() &&
           !normalizedUuid(snapshot.password).isEmpty();
}

bool readSnapshot(const QString &path, Snapshot *snapshot, QString *error)
{
    QSettings settings(path, QSettings::IniFormat);
    Snapshot loaded;
    loaded.localId = normalizedUuid(
        settings.value(QStringLiteral("local/local_id")).toString());
    loaded.password = normalizedUuid(
        settings.value(QStringLiteral("local/local_pwd")).toString());
    if (settings.status() != QSettings::NoError)
    {
        setError(error, QStringLiteral("Failed to read identity settings."));
        return false;
    }
    if (snapshot)
        *snapshot = loaded;
    return true;
}

bool serializeSnapshot(const QString &directory,
                       const Snapshot &snapshot,
                       QByteArray *serialized,
                       QString *error)
{
    QTemporaryFile staging(QDir(directory).filePath(QStringLiteral(".id-stage-XXXXXX")));
    if (!staging.open())
    {
        setError(error, QStringLiteral("Failed to create the identity staging file."));
        return false;
    }
    const QString stagingPath = staging.fileName();
    staging.setAutoRemove(false);
    if (!staging.remove())
    {
        setError(error, QStringLiteral("Failed to prepare the identity staging path."));
        return false;
    }

    QSettings settings(stagingPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("local/local_id"), snapshot.localId);
    settings.setValue(QStringLiteral("local/local_pwd"), snapshot.password);
    settings.sync();
    if (settings.status() != QSettings::NoError)
    {
        QFile::remove(stagingPath);
        setError(error, QStringLiteral("Failed to serialize identity settings."));
        return false;
    }

    QFile input(stagingPath);
    if (!input.open(QIODevice::ReadOnly))
    {
        setError(error, QStringLiteral("Failed to read the identity staging file."));
        return false;
    }
    *serialized = input.readAll();
    input.close();
    QFile::remove(stagingPath);
    if (serialized->isEmpty())
    {
        setError(error, QStringLiteral("Serialized identity settings are empty."));
        return false;
    }
    return true;
}

bool writeSnapshot(const QString &path,
                   const Snapshot &requested,
                   Snapshot *verified,
                   QString *error)
{
    Snapshot normalized{normalizedUuid(requested.localId), normalizedUuid(requested.password)};
    if (!validSnapshot(normalized))
    {
        setError(error, QStringLiteral("Identity ID and password must both be valid UUIDs."));
        return false;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath()))
    {
        setError(error, QStringLiteral("Failed to create the identity directory."));
        return false;
    }

    QByteArray serialized;
    if (!serializeSnapshot(info.absolutePath(), normalized, &serialized, error))
        return false;

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(serialized) != serialized.size())
    {
        output.cancelWriting();
        setError(error, QStringLiteral("Failed to write identity settings."));
        return false;
    }
    if (!output.commit())
    {
        setError(error, QStringLiteral("Failed to commit identity settings atomically."));
        return false;
    }

    Snapshot readBack;
    if (!readSnapshot(path, &readBack, error) ||
        readBack.localId != normalized.localId || readBack.password != normalized.password)
    {
        setError(error, QStringLiteral("Identity read-back verification failed."));
        return false;
    }
    if (verified)
        *verified = readBack;
    return true;
}

template <typename Mutation>
bool updateSnapshot(const QString &path,
                    Mutation mutation,
                    Snapshot *snapshot,
                    QString *error)
{
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(kLockTimeoutMs))
    {
        setError(error, QStringLiteral("Identity settings are locked by another process."));
        return false;
    }
    Snapshot current;
    if (!readSnapshot(path, &current, error) || !validSnapshot(current))
    {
        setError(error, QStringLiteral("Stored identity settings are invalid."));
        return false;
    }

    const Snapshot requested = mutation(current);
    return writeSnapshot(path, requested, snapshot, error);
}
} /* namespace */

bool loadOrCreate(const QString &path,
                  const QString &legacyLocalId,
                  Snapshot *snapshot,
                  QString *error)
{
    QLockFile lock(path + QStringLiteral(".lock"));
    lock.setStaleLockTime(30000);
    if (!lock.tryLock(kLockTimeoutMs))
    {
        setError(error, QStringLiteral("Identity settings are locked by another process."));
        return false;
    }
    Snapshot stored;
    const bool exists = QFileInfo::exists(path);
    if (exists && !readSnapshot(path, &stored, error))
        return false;
    if (exists && validSnapshot(stored))
    {
        if (snapshot)
            *snapshot = stored;
        return true;
    }

    Snapshot requested;
    requested.localId = normalizedUuid(legacyLocalId);
    if (requested.localId.isEmpty())
        requested.localId = normalizedUuid(stored.localId);
    if (requested.localId.isEmpty())
        requested.localId = newUuid();

    requested.password = normalizedUuid(stored.password);
    if (requested.password.isEmpty())
        requested.password = newUuid();
    return writeSnapshot(path, requested, snapshot, error);
}

bool replaceLocalId(const QString &path,
                    const QString &localId,
                    Snapshot *snapshot,
                    QString *error)
{
    const QString normalized = normalizedUuid(localId);
    if (normalized.isEmpty())
    {
        setError(error, QStringLiteral("The replacement local ID is invalid."));
        return false;
    }
    return updateSnapshot(
        path,
        [&normalized](Snapshot current) {
            current.localId = normalized;
            return current;
        },
        snapshot,
        error);
}

bool replacePassword(const QString &path,
                     const QString &password,
                     Snapshot *snapshot,
                     QString *error)
{
    const QString normalized = normalizedUuid(password);
    if (normalized.isEmpty())
    {
        setError(error, QStringLiteral("The replacement password is invalid."));
        return false;
    }
    return updateSnapshot(
        path,
        [&normalized](Snapshot current) {
            current.password = normalized;
            return current;
        },
        snapshot,
        error);
}
} /* namespace IdentityStorage */
