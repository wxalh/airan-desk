#ifndef APP_STYLE_H
#define APP_STYLE_H

#include <QtGlobal>

class QApplication;
class QWidget;
class QSize;

enum class AppTheme
{
    Light,
    Dark
};

void applyGlobalStyle(QApplication &app);
AppTheme appTheme();
qreal appScale();
void setAppTheme(AppTheme theme);
void setAppScale(qreal scale);

#endif /* APP_STYLE_H */
