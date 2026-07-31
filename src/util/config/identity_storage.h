#ifndef IDENTITY_STORAGE_H
#define IDENTITY_STORAGE_H

#include <QString>

namespace IdentityStorage
{
struct Snapshot
{
    QString localId;
    QString password;
};

bool loadOrCreate(const QString &path,
                  const QString &legacyLocalId,
                  Snapshot *snapshot,
                  QString *error = nullptr);
bool replaceLocalId(const QString &path,
                    const QString &localId,
                    Snapshot *snapshot,
                    QString *error = nullptr);
bool replacePassword(const QString &path,
                     const QString &password,
                     Snapshot *snapshot,
                     QString *error = nullptr);
} /* namespace IdentityStorage */

#endif /* IDENTITY_STORAGE_H */
