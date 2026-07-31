#ifndef PASSWORD_LINE_EDIT_UTIL_H
#define PASSWORD_LINE_EDIT_UTIL_H

class QLineEdit;
class QFont;

void installPasswordRevealButton(QLineEdit *lineEdit, bool showClearButton = false);
void setPasswordRevealFonts(QLineEdit *lineEdit, const QFont &hiddenFont, const QFont &visibleFont);

#endif /* PASSWORD_LINE_EDIT_UTIL_H */
