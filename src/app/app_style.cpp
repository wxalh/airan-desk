#include "app_style.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QPalette>
#include <QRegularExpression>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QStringList>

#include "util/config/config_util.h"

namespace
{

QString readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return QString::fromUtf8(file.readAll());
}


QStringList globalStyleCandidates(AppTheme theme)
{
    QStringList qssCandidates;

    const QString styleName = theme == AppTheme::Dark ? QStringLiteral("app_dark.qss")
                                                       : QStringLiteral("app.qss");
    const QString homeDir = QDir::homePath();
    if (!homeDir.isEmpty())
        qssCandidates << QDir(homeDir).filePath(QStringLiteral(".wxalh/airan-desk/conf/") + styleName);
    qssCandidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("conf/") + styleName);
    qssCandidates << QStringLiteral(":/conf/") + styleName;

    qssCandidates.removeDuplicates();
    return qssCandidates;
}

QSettings *appearanceSettings()
{
    return ConfigUtil && ConfigUtil->m_commonIni ? ConfigUtil->m_commonIni : nullptr;
}

qreal boundedScale(qreal scale)
{
    return qIsFinite(scale) ? qBound<qreal>(0.8, scale, 1.4) : 1.0;
}

AppTheme storedTheme(QSettings *settings)
{
    if (!settings)
        return AppTheme::Light;
    settings->beginGroup(QStringLiteral("appearance"));
    const QString theme = settings->value(QStringLiteral("theme"), QStringLiteral("light")).toString();
    settings->endGroup();
    return theme.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0 ? AppTheme::Dark : AppTheme::Light;
}

qreal storedScale(QSettings *settings)
{
    if (!settings)
        return 1.0;
    settings->beginGroup(QStringLiteral("appearance"));
    const qreal scale = settings->value(QStringLiteral("scale"), 1.0).toDouble();
    settings->endGroup();
    return boundedScale(scale);
}

QString scalePixelValues(QString stylesheet, qreal scale)
{
    const QRegularExpression sizes(QStringLiteral("(-?\\d+(?:\\.\\d+)?)(px|pt)"));
    const QRegularExpressionMatchIterator matches = sizes.globalMatch(stylesheet);
    QVector<QPair<int, QString>> replacements;
    for (auto it = matches; it.hasNext();)
    {
        const QRegularExpressionMatch match = it.next();
        replacements.append(qMakePair(match.capturedStart(),
                                      (match.captured(2) == QStringLiteral("pt")
                                           ? QString::number(match.captured(1).toDouble() * scale, 'f', 1)
                                           : QString::number(qRound(match.captured(1).toDouble() * scale))) + match.captured(2)));
    }
    for (auto it = replacements.crbegin(); it != replacements.crend(); ++it)
    {
        const QRegularExpressionMatch match = sizes.match(stylesheet, it->first);
        if (match.hasMatch())
            stylesheet.replace(match.capturedStart(), match.capturedLength(), it->second);
    }
    return stylesheet;
}
} /* namespace */


void applyGlobalStyle(QApplication &app)
{
    const AppTheme theme = storedTheme(appearanceSettings());
    const qreal scale = storedScale(appearanceSettings());
    app.setProperty("airanDarkTheme", theme == AppTheme::Dark);
    app.setProperty("airanUiScaleFactor", scale);

    qreal basePointSize = app.property("airanBaseFontPointSize").toDouble();
    if (basePointSize <= 0.0)
    {
        basePointSize = app.font().pointSizeF();
        if (basePointSize <= 0.0)
            basePointSize = 10.0;
        app.setProperty("airanBaseFontPointSize", basePointSize);
    }
    QFont font = app.font();
    font.setPointSizeF(basePointSize * scale);
    app.setFont(font);

    QPalette palette;
    if (theme == AppTheme::Dark)
    {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#202124")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e8eaed")));
        palette.setColor(QPalette::Base, QColor(QStringLiteral("#2a2d30")));
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#303438")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#e8eaed")));
        palette.setColor(QPalette::Button, QColor(QStringLiteral("#303438")));
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e8eaed")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#409eff")));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    else
    {
        palette.setColor(QPalette::Window, QColor(QStringLiteral("#f5f7fa")));
        palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#303133")));
        palette.setColor(QPalette::Base, Qt::white);
        palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f5f7fa")));
        palette.setColor(QPalette::Text, QColor(QStringLiteral("#303133")));
        palette.setColor(QPalette::Button, Qt::white);
        palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#606266")));
        palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#409eff")));
        palette.setColor(QPalette::HighlightedText, Qt::white);
    }
    app.setPalette(palette);

    for (const QString &path : globalStyleCandidates(theme))
    {
        const QString stylesheet = readTextFile(path);
        if (!stylesheet.isEmpty())
        {
            app.setStyleSheet(scalePixelValues(stylesheet, scale));
            return;
        }
    }
}

AppTheme appTheme()
{
    return qApp && qApp->property("airanDarkTheme").toBool() ? AppTheme::Dark : AppTheme::Light;
}

qreal appScale()
{
    const QVariant value = qApp ? qApp->property("airanUiScaleFactor") : QVariant();
    return value.isValid() ? boundedScale(value.toDouble()) : 1.0;
}

void setAppTheme(AppTheme theme)
{
    if (QSettings *settings = appearanceSettings())
    {
        settings->beginGroup(QStringLiteral("appearance"));
        settings->setValue(QStringLiteral("theme"), theme == AppTheme::Dark ? QStringLiteral("dark")
                                                                            : QStringLiteral("light"));
        settings->endGroup();
        settings->sync();
    }
    if (qApp)
        applyGlobalStyle(*qApp);
}

void setAppScale(qreal scale)
{
    scale = boundedScale(scale);
    if (QSettings *settings = appearanceSettings())
    {
        settings->beginGroup(QStringLiteral("appearance"));
        settings->setValue(QStringLiteral("scale"), scale);
        settings->endGroup();
        settings->sync();
    }
    if (qApp)
        applyGlobalStyle(*qApp);
}
