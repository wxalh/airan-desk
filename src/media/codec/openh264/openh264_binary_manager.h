#pragma once

#include <QByteArray>
#include <QString>

namespace airan::media::openh264
{

enum class Availability
{
    Disabled,
    Missing,
    Rejected,
    RestartRequired,
    Ready
};

struct ReleaseBinary
{
    QString version;
    QString platform;
    QString architecture;
    QString fileName;
    qint64 fileSize{0};
    QByteArray sha256;
    QString downloadUrl;
    QString releaseUrl;
};

struct ValidationResult
{
    Availability availability{Availability::Missing};
    QString absolutePath;
    QString reasonCode;
};

const ReleaseBinary *currentReleaseBinary();
QString defaultStorageRoot();
QString managedLibraryPath(const QString &storageRoot = QString());
bool isManagedLibraryPath(const QString &path,
                          const QString &storageRoot = QString());

ValidationResult validateOfficialBinary(const QString &path);
ValidationResult validateManagedBinary(const QString &path,
                                       const QString &storageRoot = QString());
ValidationResult installOfficialBinary(const QString &sourcePath,
                                       const QString &storageRoot = QString());
ValidationResult currentAvailability(bool enabled,
                                     const QString &configuredPath,
                                     const QString &storageRoot = QString());

} // namespace airan::media::openh264
