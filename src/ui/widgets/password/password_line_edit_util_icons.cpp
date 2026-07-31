#include "password_line_edit_util_internal.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace PasswordLineEditInternal
{
QIcon makeEyeIcon(bool visible)
{
    QPixmap pixmap(kIconCanvasSize, kIconCanvasSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QColor color(131, 193, 224);
    QPen pen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    QPainterPath eyePath;
    eyePath.moveTo(2.5, 9.0);
    eyePath.cubicTo(5.0, 4.8, 13.0, 4.8, 15.5, 9.0);
    eyePath.cubicTo(13.0, 13.2, 5.0, 13.2, 2.5, 9.0);
    painter.drawPath(eyePath);
    painter.drawEllipse(QPointF(9.0, 9.0), 2.2, 2.2);

    if (!visible)
        painter.drawLine(QPointF(3.5, 14.0), QPointF(14.5, 4.0));

    return QIcon(pixmap);
}
} /* namespace PasswordLineEditInternal */
