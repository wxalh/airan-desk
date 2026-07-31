#include "password_line_edit_util.h"
#include "password_line_edit_util_internal.h"

#include <QCoreApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QLineEdit>
#include <QMargins>
#include <QScreen>
#include <QSize>
#include <QStyle>
#include <QToolButton>
#include <QWidget>
#include <QWindow>

namespace
{

class PasswordLineEditFilter : public QObject
{
public:
    
    explicit PasswordLineEditFilter(QLineEdit *lineEdit, QToolButton *eyeButton, QToolButton *clearButton)
        : QObject(lineEdit), m_lineEdit(lineEdit), m_eyeButton(eyeButton), m_clearButton(clearButton)
    {
    }

protected:
    
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_lineEdit &&
            (event->type() == QEvent::Resize ||
             event->type() == QEvent::Show ||
             event->type() == QEvent::StyleChange ||
             event->type() == QEvent::ScreenChangeInternal
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
             || event->type() == QEvent::DevicePixelRatioChange
#endif
                 ))
            PasswordLineEditInternal::polishPasswordEmbeddedButtons(m_lineEdit, m_eyeButton, m_clearButton);
        return QObject::eventFilter(watched, event);
    }

private:
    QLineEdit *m_lineEdit{nullptr};
    QToolButton *m_eyeButton{nullptr};
    QToolButton *m_clearButton{nullptr};
};
} /* namespace */

namespace PasswordLineEditInternal
{
namespace
{
int scaledForWidget(QWidget *widget, int value)
{
    QScreen *screen = nullptr;
    if (widget && widget->windowHandle())
        screen = widget->windowHandle()->screen();
    if (!screen && widget && widget->window() && widget->window()->windowHandle())
        screen = widget->window()->windowHandle()->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const double scale = screen ? qBound(0.75, screen->logicalDotsPerInch() / 96.0, 3.0) : 1.0;
    return qMax(1, static_cast<int>(qRound(value * scale)));
}
} // namespace

void polishPasswordEmbeddedButtons(QLineEdit *lineEdit, QToolButton *eyeButton, QToolButton *clearButton)
{
    if (!lineEdit || !eyeButton)
        return;

    const int iconSize = scaledForWidget(lineEdit, kIconSize);
    const int buttonSize = scaledForWidget(lineEdit, 22);
    const int edgeInset = scaledForWidget(lineEdit, 6);
    const int overlapAdjust = scaledForWidget(lineEdit, 2);
    const int buttonGap = scaledForWidget(lineEdit, 4);
    const int hoverRadius = buttonSize / 2;

    for (QToolButton *button : {eyeButton, clearButton})
    {
        if (!button)
            continue;

        button->setCursor(Qt::ArrowCursor);
        button->setIconSize(QSize(iconSize, iconSize));
        button->setFixedSize(buttonSize, buttonSize);
        button->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "    background: transparent;"
            "    border: none;"
            "    padding: 0;"
            "    margin: 0;"
            "}"
            "QToolButton:hover {"
            "    background: rgba(131, 193, 224, 0.14);"
            "    border-radius: %1px;"
            "}")
                                  .arg(hoverRadius));
    }

    const int top = qMax(0, (lineEdit->height() - buttonSize) / 2);
    if (clearButton)
    {
        const int clearX = lineEdit->width() - edgeInset - clearButton->width() + overlapAdjust;
        const int eyeX = clearX - buttonGap - eyeButton->width();
        clearButton->move(clearX, top);
        eyeButton->move(eyeX, top);
        clearButton->raise();
        eyeButton->raise();
    }
    else
    {
        const int eyeX = lineEdit->width() - edgeInset - eyeButton->width() + overlapAdjust;
        eyeButton->move(eyeX, top);
        eyeButton->raise();
    }

    const int reserved = clearButton
                             ? edgeInset + buttonSize * 2 + buttonGap + scaledForWidget(lineEdit, 4)
                             : edgeInset + buttonSize + scaledForWidget(lineEdit, 4);
    QMargins margins = lineEdit->textMargins();
    if (margins.right() != reserved)
    {
        margins.setRight(reserved);
        lineEdit->setTextMargins(margins);
    }
}
} /* namespace PasswordLineEditInternal */


void installPasswordRevealButton(QLineEdit *lineEdit, bool showClearButton)
{
    if (!lineEdit)
        return;

    lineEdit->setClearButtonEnabled(false);
    lineEdit->setEchoMode(QLineEdit::Password);
    if (!lineEdit->property(PasswordLineEditInternal::kPasswordHiddenFontProperty).isValid())
        lineEdit->setProperty(PasswordLineEditInternal::kPasswordHiddenFontProperty, lineEdit->font());
    if (!lineEdit->property(PasswordLineEditInternal::kPasswordVisibleFontProperty).isValid())
        lineEdit->setProperty(PasswordLineEditInternal::kPasswordVisibleFontProperty, lineEdit->font());
    PasswordLineEditInternal::setPasswordStyleActive(lineEdit, true);

    auto *eyeButton = new QToolButton(lineEdit);
    eyeButton->setIcon(PasswordLineEditInternal::makeEyeIcon(false));
    eyeButton->setToolTip(QLineEdit::tr("Show password"));
    eyeButton->setFocusPolicy(Qt::NoFocus);
    eyeButton->setCursor(Qt::ArrowCursor);
    eyeButton->show();

    QToolButton *clearButton = nullptr;
    if (showClearButton)
    {
        clearButton = new QToolButton(lineEdit);
        clearButton->setIcon(lineEdit->style()->standardIcon(QStyle::SP_LineEditClearButton));
        clearButton->setToolTip(QLineEdit::tr("Clear"));
        clearButton->setFocusPolicy(Qt::NoFocus);
        clearButton->setCursor(Qt::ArrowCursor);
        clearButton->show();
        QObject::connect(clearButton, &QToolButton::clicked, lineEdit, &QLineEdit::clear);
    }

    auto *filter = new PasswordLineEditFilter(lineEdit, eyeButton, clearButton);
    lineEdit->installEventFilter(filter);
    QEvent showEvent(QEvent::Show);
    QCoreApplication::sendEvent(lineEdit, &showEvent);

    QObject::connect(eyeButton, &QToolButton::clicked, lineEdit, [lineEdit, eyeButton](bool) {
        const bool show = lineEdit->echoMode() == QLineEdit::Password;
        PasswordLineEditInternal::applyPasswordRevealState(lineEdit, eyeButton, show);
    });

    if (clearButton)
    {
        QObject::connect(lineEdit, &QLineEdit::textChanged, clearButton, [clearButton](const QString &text) {
            clearButton->setVisible(!text.isEmpty());
        });
        clearButton->setVisible(!lineEdit->text().isEmpty());
    }

    PasswordLineEditInternal::setPasswordStyleActive(lineEdit, true);
    QCoreApplication::sendEvent(lineEdit, &showEvent);
}
