#include "password_line_edit_util.h"
#include "password_line_edit_util_internal.h"

#include <QLineEdit>
#include <QStyle>
#include <QToolButton>
#include <QVariant>

namespace PasswordLineEditInternal
{
void setPasswordStyleActive(QLineEdit *lineEdit, bool active)
{
    if (!lineEdit)
        return;

    lineEdit->setProperty("passwordField", active);
    lineEdit->style()->unpolish(lineEdit);
    lineEdit->style()->polish(lineEdit);
    lineEdit->update();
}

QFont passwordFontProperty(QLineEdit *lineEdit, const char *name, const QFont &fallback)
{
    const QVariant value = lineEdit ? lineEdit->property(name) : QVariant();
    return value.canConvert<QFont>() ? value.value<QFont>() : fallback;
}

void applyPasswordRevealState(QLineEdit *lineEdit, QToolButton *eyeButton, bool show)
{
    if (!lineEdit || !eyeButton)
        return;

    lineEdit->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
    setPasswordStyleActive(lineEdit, !show);
    lineEdit->setFont(passwordFontProperty(lineEdit,
                                           show ? kPasswordVisibleFontProperty : kPasswordHiddenFontProperty,
                                           lineEdit->font()));
    eyeButton->setIcon(makeEyeIcon(show));
    eyeButton->setToolTip(show ? QLineEdit::tr("Hide password")
                               : QLineEdit::tr("Show password"));
}
} /* namespace PasswordLineEditInternal */


void setPasswordRevealFonts(QLineEdit *lineEdit, const QFont &hiddenFont, const QFont &visibleFont)
{
    if (!lineEdit)
        return;

    lineEdit->setProperty(PasswordLineEditInternal::kPasswordHiddenFontProperty, hiddenFont);
    lineEdit->setProperty(PasswordLineEditInternal::kPasswordVisibleFontProperty, visibleFont);
    lineEdit->setFont(lineEdit->echoMode() == QLineEdit::Normal ? visibleFont : hiddenFont);
}
