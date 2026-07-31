#ifndef APP_TITLE_BAR_ICONS_H
#define APP_TITLE_BAR_ICONS_H

#include <QIcon>


enum class TitleBarGlyph
{
    Minimize,
    Maximize,
    Restore,
    Close
};


QIcon makeTitleBarIcon(TitleBarGlyph glyph);

#endif /* APP_TITLE_BAR_ICONS_H */
