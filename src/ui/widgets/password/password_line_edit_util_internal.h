#ifndef PASSWORD_LINE_EDIT_UTIL_INTERNAL_H
#define PASSWORD_LINE_EDIT_UTIL_INTERNAL_H

#include <QFont>
#include <QIcon>

class QLineEdit;
class QToolButton;

namespace PasswordLineEditInternal
{
constexpr int kIconSize = 14;
constexpr int kIconCanvasSize = 18;
constexpr const char *kPasswordHiddenFontProperty = "_airanPasswordHiddenFont";
constexpr const char *kPasswordVisibleFontProperty = "_airanPasswordVisibleFont";


QIcon makeEyeIcon(bool visible);


void setPasswordStyleActive(QLineEdit *lineEdit, bool active);


QFont passwordFontProperty(QLineEdit *lineEdit, const char *name, const QFont &fallback);


void applyPasswordRevealState(QLineEdit *lineEdit, QToolButton *eyeButton, bool show);


void polishPasswordEmbeddedButtons(QLineEdit *lineEdit, QToolButton *eyeButton, QToolButton *clearButton);
} /* namespace PasswordLineEditInternal */

#endif /* PASSWORD_LINE_EDIT_UTIL_INTERNAL_H */
