#include "settings_window.h"

#include "util/config/config_util.h"
#include "util/text/i18n_util.h"

#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextBrowser>
#include <QUrl>

namespace
{
const QUrl kLatestReleaseUrl(QStringLiteral("https://github.com/wxalh/airan-desk/releases/latest"));
const QUrl kProjectUrl(QStringLiteral("https://github.com/wxalh/airan-desk"));
const QUrl kServerProjectUrl(QStringLiteral("https://github.com/wxalh/signal-server"));
const QUrl kChineseReadmeUrl(QStringLiteral("https://raw.githubusercontent.com/wxalh/airan-desk/main/README.md"));
const QUrl kEnglishReadmeUrl(QStringLiteral("https://raw.githubusercontent.com/wxalh/airan-desk/main/README.en.md"));

QString readLocalReadme(const QString &fileName)
{
    QStringList candidates{
        QDir(QCoreApplication::applicationDirPath()).filePath(fileName),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/airan-desk/%1").arg(fileName))};
#if defined(AIRAN_DESK_INSTALL_DATADIR)
    candidates.append(QDir(QStringLiteral(AIRAN_DESK_INSTALL_DATADIR)).filePath(fileName));
#endif

    for (const QString &path : candidates)
    {
        QFile file(QDir::cleanPath(path));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        return QString::fromUtf8(file.readAll());
    }
    return {};
}
} /* namespace */

void SettingsWindow::populateAboutSettings()
{
    m_aboutVersionLabel->setText(
        tr("Version: %1").arg(QCoreApplication::applicationVersion()));

    const bool chinese = I18nUtil::resolveUiLanguage(ConfigUtil->language) == I18nUtil::zhCnLanguageKey();
    const QString readmeName = chinese ? QStringLiteral("README.md") : QStringLiteral("README.en.md");
    const QUrl readmeUrl = chinese ? kChineseReadmeUrl : kEnglishReadmeUrl;
    const QString localReadme = readLocalReadme(readmeName);
    if (!localReadme.isEmpty())
    {
        m_aboutReadmeBrowser->setPlainText(localReadme);
        return;
    }

    m_aboutReadmeBrowser->setPlainText(tr("Local README not found. Loading the online README..."));
    if (!m_aboutNetworkManager)
        m_aboutNetworkManager = new QNetworkAccessManager(this);
    QNetworkReply *reply = m_aboutNetworkManager->get(QNetworkRequest(readmeUrl));
    connect(reply, &QNetworkReply::finished, this, [this, reply, readmeUrl]() {
        if (reply->error() == QNetworkReply::NoError)
        {
            m_aboutReadmeBrowser->setPlainText(QString::fromUtf8(reply->readAll()));
        }
        else
        {
            m_aboutReadmeBrowser->setPlainText(
                tr("README could not be loaded. Open the online file: %1").arg(readmeUrl.toString()));
        }
        reply->deleteLater();
    });
}

bool SettingsWindow::openAboutUrl(const QUrl &url, const QString &actionName)
{
    if (QDesktopServices::openUrl(url))
        return true;

    QMessageBox::warning(this,
                         tr("About Airan Desk"),
                         tr("The %1 page could not be opened.").arg(actionName));
    return false;
}

void SettingsWindow::openLatestReleasePage()
{
    openAboutUrl(kLatestReleaseUrl, tr("latest release"));
}

void SettingsWindow::openProjectPage()
{
    openAboutUrl(kProjectUrl, tr("project"));
}

void SettingsWindow::openStarPage()
{
    openAboutUrl(kProjectUrl, tr("GitHub Star"));
}

void SettingsWindow::openServerPage()
{
    openAboutUrl(kServerProjectUrl, tr("signal server project"));
}
