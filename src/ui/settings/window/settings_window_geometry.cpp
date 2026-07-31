#include "settings_window.h"

#include "ui/common/adaptive_ui.h"

#include <QRect>
#include <QScreen>


void SettingsWindow::centerOnParent()
{
    QWidget *parentWindow = parentWidget() ? parentWidget()->window() : nullptr;
    QRect anchor = parentWindow ? parentWindow->frameGeometry()
                                : UiAdaptive::availableGeometry(this);

    QRect target(QPoint(0, 0), size());
    target.moveCenter(anchor.center());

    QScreen *targetScreen = UiAdaptive::screenForWidget(parentWindow ? parentWindow : this);
    const QRect available = targetScreen ? targetScreen->availableGeometry()
                                         : UiAdaptive::availableGeometry(this);
    if (target.left() < available.left())
        target.moveLeft(available.left());
    if (target.top() < available.top())
        target.moveTop(available.top());
    if (target.right() > available.right())
        target.moveRight(available.right());
    if (target.bottom() > available.bottom())
        target.moveBottom(available.bottom());

    move(target.topLeft());
}
