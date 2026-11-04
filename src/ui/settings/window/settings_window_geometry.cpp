#include "settings_window.h"

#include "ui/common/adaptive_ui.h"

#include <QRect>
#include <QScreen>


void SettingsWindow::centerOnParent()
{
    UiAdaptive::centerWindowOnScreen(this);
}
