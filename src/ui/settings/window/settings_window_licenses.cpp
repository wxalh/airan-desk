#include "settings_window.h"

#include "ui/settings/licenses/license_viewer_dialog.h"

void SettingsWindow::showThirdPartyLicenses()
{
    LicenseViewerDialog dialog(this);
    dialog.exec();
}
