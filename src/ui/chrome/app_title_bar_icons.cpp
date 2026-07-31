#include "app_title_bar_icons.h"

#include <QPainter>
#include <QPixmap>

QIcon makeTitleBarIcon(TitleBarGlyph glyph)
{
    QPixmap pixmap(16, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor(131, 193, 224), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    switch (glyph)
    {
    case TitleBarGlyph::Minimize:
        painter.drawLine(QPointF(4, 11), QPointF(12, 11));
        break;
    case TitleBarGlyph::Maximize:
        painter.drawRect(QRectF(4.5, 4.5, 7, 7));
        break;
    case TitleBarGlyph::Restore:
        painter.drawRect(QRectF(5.5, 6.5, 6, 6));
        painter.drawLine(QPointF(7, 4.5), QPointF(12.5, 4.5));
        painter.drawLine(QPointF(12.5, 4.5), QPointF(12.5, 10));
        break;
    case TitleBarGlyph::Close:
        painter.drawLine(QPointF(5, 5), QPointF(11, 11));
        painter.drawLine(QPointF(11, 5), QPointF(5, 11));
        break;
    }

    return QIcon(pixmap);
}
