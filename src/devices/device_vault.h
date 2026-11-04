#ifndef DEVICE_VAULT_H
#define DEVICE_VAULT_H

#include <QByteArray>
#include <QString>
#include <QVector>

struct DeviceEntry
{
    QString id;
    QString password;
    QString note;
    QString group;
    QString os;
};

class DeviceVault
{
public:
    explicit DeviceVault(const QString &path = QString());
    ~DeviceVault();
    DeviceVault(const DeviceVault &) = delete;
    DeviceVault &operator=(const DeviceVault &) = delete;

    bool exists() const;
    bool isUnlocked() const;
    bool isRemembered() const;
    bool create(const QString &password, bool remember = false);
    bool unlock(const QString &password);
    bool unlockRemembered();
    bool setRemembered(bool remember);
    bool save(const QVector<DeviceEntry> &entries);
    const QVector<DeviceEntry> &entries() const;
    void lock();
    bool changePassword(const QString &oldPassword, const QString &newPassword);
    QString errorString() const;
    QString path() const;

private:
    QString m_path;
    QString m_error;
    QByteArray m_key;
    QByteArray m_salt;
    QByteArray m_revision;
    QVector<DeviceEntry> m_entries;
};

#endif
