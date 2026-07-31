#include "settings_window.h"

#include "media/codec/openh264/openh264_binary_manager.h"
#include "util/config/config_util.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
using airan::media::openh264::Availability;
using airan::media::openh264::ValidationResult;

QString translatedReason(const QString &reasonCode)
{
    if (reasonCode == QStringLiteral("unsupported-platform"))
        return SettingsWindow::tr("This platform is not supported by the pinned Cisco release.");
    if (reasonCode == QStringLiteral("missing"))
        return SettingsWindow::tr("No managed OpenH264 binary is installed.");
    if (reasonCode == QStringLiteral("unexpected-file-name"))
        return SettingsWindow::tr("The selected file name does not match the official Cisco binary.");
    if (reasonCode == QStringLiteral("size-mismatch"))
        return SettingsWindow::tr("The selected file size does not match the official Cisco binary.");
    if (reasonCode == QStringLiteral("symlink-rejected"))
        return SettingsWindow::tr("Symbolic links and reparse points are not accepted.");
    if (reasonCode == QStringLiteral("not-regular-file"))
        return SettingsWindow::tr("The selected path is not a regular file.");
    if (reasonCode == QStringLiteral("unreadable"))
        return SettingsWindow::tr("The selected file could not be read.");
    if (reasonCode == QStringLiteral("invalid-binary-format"))
        return SettingsWindow::tr("The selected file is not a supported executable binary.");
    if (reasonCode == QStringLiteral("wrong-architecture"))
        return SettingsWindow::tr("The selected binary does not match this platform and CPU architecture.");
    if (reasonCode == QStringLiteral("hash-mismatch"))
        return SettingsWindow::tr("The selected file does not match Cisco's published binary.");
    if (reasonCode == QStringLiteral("canonical-path-failed"))
        return SettingsWindow::tr("The selected file path could not be resolved safely.");
    if (reasonCode == QStringLiteral("runtime-incompatible"))
        return SettingsWindow::tr("The selected binary does not provide the required OpenH264 interface.");
    if (reasonCode == QStringLiteral("unmanaged-path"))
        return SettingsWindow::tr("Only an OpenH264 binary installed by Airan Desk can be enabled.");
    if (reasonCode == QStringLiteral("storage-unavailable"))
        return SettingsWindow::tr("The managed codec directory could not be created safely.");
    if (reasonCode == QStringLiteral("copy-failed"))
        return SettingsWindow::tr("The binary could not be installed atomically.");
    return SettingsWindow::tr("OpenH264 is unavailable.");
}

QString statusText(const ValidationResult &result)
{
    switch (result.availability)
    {
    case Availability::Disabled:
        return SettingsWindow::tr("Disabled");
    case Availability::Missing:
        return SettingsWindow::tr("Not installed");
    case Availability::Rejected:
        return SettingsWindow::tr("Unavailable: %1").arg(translatedReason(result.reasonCode));
    case Availability::RestartRequired:
        return SettingsWindow::tr("Installed; restart required");
    case Availability::Ready:
        return SettingsWindow::tr("Ready");
    }
    return SettingsWindow::tr("Unavailable");
}
} // namespace

void SettingsWindow::populateCodecSettings()
{
    if (!ConfigUtil->loadPendingOpenH264Config(
            &m_pendingOpenH264Enabled, &m_pendingOpenH264Path))
    {
        m_pendingOpenH264Enabled = ConfigUtil->openh264_enabled;
        m_pendingOpenH264Path = ConfigUtil->openh264_library_path;
    }
    m_openh264EnableCheck->setChecked(m_pendingOpenH264Enabled);
    refreshOpenH264Status();
}

void SettingsWindow::refreshOpenH264Status()
{
    m_pendingOpenH264Enabled = m_openh264EnableCheck->isChecked();
    const ValidationResult availability = airan::media::openh264::currentAvailability(
        m_pendingOpenH264Enabled, m_pendingOpenH264Path);
    const bool differsFromRuntime =
        m_pendingOpenH264Enabled != ConfigUtil->openh264_enabled ||
        m_pendingOpenH264Path != ConfigUtil->openh264_library_path;
    const bool validPendingChange =
        !m_pendingOpenH264Enabled || availability.availability == Availability::Ready;
    if (differsFromRuntime && validPendingChange)
        m_openh264StatusLabel->setText(tr("Restart Airan Desk to apply this change."));
    else
        m_openh264StatusLabel->setText(statusText(availability));
}

void SettingsWindow::chooseOpenH264Binary()
{
    const airan::media::openh264::ReleaseBinary *release =
        airan::media::openh264::currentReleaseBinary();
    const QString selected = QFileDialog::getOpenFileName(
        this,
        tr("Import Cisco OpenH264 binary"),
        release ? release->fileName : QString(),
#if defined(Q_OS_WIN)
        tr("Dynamic libraries (*.dll);;All files (*)")
#elif defined(Q_OS_MACOS)
        tr("Dynamic libraries (*.dylib);;All files (*)")
#else
        tr("Dynamic libraries (*.so *.so.*);;All files (*)")
#endif
    );
    if (selected.isEmpty())
        return;

    const ValidationResult installed =
        airan::media::openh264::installOfficialBinary(selected);
    if (installed.availability != Availability::RestartRequired ||
        installed.absolutePath.isEmpty())
    {
        QMessageBox::critical(this, tr("OpenH264 import"), translatedReason(installed.reasonCode));
        refreshOpenH264Status();
        return;
    }

    m_pendingOpenH264Path = installed.absolutePath;
    m_openh264StatusLabel->setText(statusText(installed));
    QMessageBox::information(
        this,
        tr("OpenH264 import"),
        tr("The Cisco OpenH264 binary was installed. Save settings and restart Airan Desk to use it."));
}

void SettingsWindow::openOpenH264ReleasePage()
{
    const airan::media::openh264::ReleaseBinary *release =
        airan::media::openh264::currentReleaseBinary();
    const QString url = release && !release->releaseUrl.isEmpty()
                            ? release->releaseUrl
                            : QStringLiteral("https://github.com/cisco/openh264/releases");
    if (!QDesktopServices::openUrl(QUrl(url)))
    {
        QMessageBox::warning(this, tr("OpenH264 download"),
                             tr("The Cisco OpenH264 releases page could not be opened."));
    }
}

void SettingsWindow::showOpenH264License()
{
    QFile licenseFile(QStringLiteral(":/licenses/Cisco-OpenH264-BINARY_LICENSE.txt"));
    if (!licenseFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(this, tr("Cisco OpenH264 license"),
                              tr("The bundled Cisco OpenH264 license could not be opened."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Cisco OpenH264 binary license"));
    dialog.resize(680, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setPlainText(QString::fromUtf8(licenseFile.readAll()));
    layout->addWidget(text);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}
