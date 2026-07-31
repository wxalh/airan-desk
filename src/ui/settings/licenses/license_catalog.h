#ifndef LICENSE_CATALOG_H
#define LICENSE_CATALOG_H

#include <QList>
#include <QString>
#include <QStringList>

namespace LicenseCatalog
{
struct Entry
{
    QString displayName;
    QString filePath;
};

QStringList defaultDirectories();
QList<Entry> discover(const QStringList &directories);
QString readText(const Entry &entry, QString *errorMessage = nullptr);
}

#endif /* LICENSE_CATALOG_H */
