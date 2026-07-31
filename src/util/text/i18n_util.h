#ifndef I18N_UTIL_H
#define I18N_UTIL_H

#include <QString>
#include <QStringList>

class QCoreApplication;
class QTranslator;

namespace I18nUtil
{
QString autoLanguageKey();
QString zhCnLanguageKey();
QString enUsLanguageKey();
QStringList supportedUiLanguages();
QString normalizeUiLanguage(const QString &language);
QString resolveUiLanguage(const QString &configuredLanguage);
QString resolveQtLanguage(const QString &configuredLanguage);
QStringList translationSearchPaths();
bool installTranslator(QCoreApplication &app, QTranslator &translator, const QString &baseName, const QString &localeName);
}

#endif /* I18N_UTIL_H */
