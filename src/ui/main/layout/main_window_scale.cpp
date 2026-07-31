#include "ui/main/main_window.h"


int MainWindow::scaled(int value) const
{
    return qMax(1, static_cast<int>(qRound(value * m_uiScale * m_dpiScale)));
}


QRect MainWindow::scaledRect(int x, int y, int width, int height) const
{
    return QRect(scaled(x), scaled(y), scaled(width), scaled(height));
}


QFont MainWindow::scaledFont(double pointSize, bool bold) const
{
    QFont font;
    font.setPointSizeF(qMax(4.5, pointSize * m_uiScale));
    font.setBold(bold);
    return font;
}
