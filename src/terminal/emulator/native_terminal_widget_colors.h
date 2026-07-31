#ifndef NATIVE_TERMINAL_WIDGET_COLORS_H
#define NATIVE_TERMINAL_WIDGET_COLORS_H

#include <QColor>
#include <array>

inline const QColor kDefaultForeground(236, 236, 236);
inline const QColor kDefaultBackground(30, 30, 30);
inline const QColor kCursorColor(180, 180, 192);
inline const QColor kSelectionForeground(255, 255, 255);
inline const QColor kSelectionBackground(38, 79, 120);

inline const std::array<QColor, 16> kAnsiPalette = {
    QColor(30, 30, 30),
    QColor(255, 96, 96),
    QColor(14, 177, 108),
    QColor(190, 190, 18),
    QColor(18, 150, 190),
    QColor(255, 77, 255),
    QColor(84, 204, 239),
    QColor(204, 204, 204),
    QColor(128, 128, 128),
    QColor(255, 128, 128),
    QColor(24, 237, 147),
    QColor(222, 220, 18),
    QColor(27, 186, 233),
    QColor(255, 125, 255),
    QColor(142, 221, 244),
    QColor(204, 204, 204),
};

#endif /* NATIVE_TERMINAL_WIDGET_COLORS_H */
