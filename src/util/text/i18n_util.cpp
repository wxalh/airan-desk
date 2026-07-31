#include "i18n_util.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSet>
#include <QTranslator>

namespace
{
QString cleanPath(const QString &path)
{
    return QDir::cleanPath(path);
}

QStringList localeNameCandidates(const QString &localeName)
{
    const QString normalized = localeName.trimmed().replace(QLatin1Char('-'), QLatin1Char('_'));
    QStringList candidates;
    if (!normalized.isEmpty())
        candidates << normalized;

    const int territorySeparator = normalized.indexOf(QLatin1Char('_'));
    if (territorySeparator > 0)
        candidates << normalized.left(territorySeparator);

    candidates.removeDuplicates();
    return candidates;
}
}

namespace I18nUtil
{
QString autoLanguageKey()
{
    return QStringLiteral("auto");
}

QString zhCnLanguageKey()
{
    return QStringLiteral("zh_CN");
}

QString enUsLanguageKey()
{
    return QStringLiteral("en_US");
}

QStringList supportedUiLanguages()
{
    return {autoLanguageKey(), zhCnLanguageKey(), enUsLanguageKey()};
}

QString normalizeUiLanguage(const QString &language)
{
    const QString normalized = language.trimmed();
    return supportedUiLanguages().contains(normalized) ? normalized : autoLanguageKey();
}

QString resolveUiLanguage(const QString &configuredLanguage)
{
    const QString normalized = normalizeUiLanguage(configuredLanguage);
    if (normalized != autoLanguageKey())
        return normalized;

    const QLocale systemLocale;
    return systemLocale.language() == QLocale::Chinese ? zhCnLanguageKey() : enUsLanguageKey();
}

QString resolveQtLanguage(const QString &configuredLanguage)
{
    const QString normalized = normalizeUiLanguage(configuredLanguage);
    if (normalized != autoLanguageKey())
        return normalized;

    return QLocale::system().name();
}

QStringList translationSearchPaths()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        cleanPath(appDir + QStringLiteral("/locale")),
        cleanPath(appDir + QStringLiteral("/translations")),
        cleanPath(appDir + QStringLiteral("/../share/airan-desk/locale")),
        cleanPath(appDir + QStringLiteral("/../share/airan-desk/translations")),
        cleanPath(appDir + QStringLiteral("/../Resources/locale")),
        cleanPath(appDir + QStringLiteral("/../Resources/translations")),
        QStringLiteral(":/locale")};

    QStringList paths;
    QSet<QString> seen;
    for (const QString &path : candidates)
    {
        if (!seen.contains(path))
        {
            paths << path;
            seen.insert(path);
        }
    }
    return paths;
}

bool installTranslator(QCoreApplication &app, QTranslator &translator, const QString &baseName, const QString &localeName)
{
    QStringList qmNames;
    for (const QString &candidate : localeNameCandidates(localeName))
        qmNames << (baseName + candidate + QStringLiteral(".qm"));

    if (baseName == QStringLiteral("qtbase_"))
    {
        for (const QString &candidate : localeNameCandidates(localeName))
            qmNames << (QStringLiteral("qt_") + candidate + QStringLiteral(".qm"));
    }
    qmNames.removeDuplicates();

    const QStringList searchPaths = translationSearchPaths();
    for (const QString &qmName : qmNames)
    {
        for (const QString &path : searchPaths)
        {
            if (translator.load(qmName, path))
            {
                app.installTranslator(&translator);
                return true;
            }
        }
    }
    return false;
}
}
