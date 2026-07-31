#include "app_style.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>

namespace
{

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return QString::fromUtf8(file.readAll());
}


QStringList globalStyleCandidates()
{
    QStringList qssCandidates;

    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty())
        qssCandidates << QDir(homeDir).filePath(QStringLiteral(".wxalh/airan-desk/conf/app.qss"));
    qssCandidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("conf/app.qss"));
    qssCandidates << QStringLiteral(":/conf/app.qss");

    qssCandidates.removeDuplicates();
    return qssCandidates;
}
} /* namespace */


void applyGlobalStyle(QApplication &app)
{
    for (const QString &path : globalStyleCandidates())
    {
        const QString qss = readTextFile(path);
        if (!qss.isEmpty())
        {
            app.setStyleSheet(qss);
            return;
        }
    }
}
