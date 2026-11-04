#ifndef DEVICE_VAULT_CREDENTIALS_H
#define DEVICE_VAULT_CREDENTIALS_H

#include <QByteArray>
#include <QString>

namespace DeviceVaultCredentials
{
bool exists(const QString &vaultPath);
bool store(const QString &vaultPath, const QByteArray &key, QString *error);
bool load(const QString &vaultPath, QByteArray *key, QString *error);
bool remove(const QString &vaultPath, QString *error);
}

#endif
