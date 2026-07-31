#include "settings_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_settings_window.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QShowEvent>
#include <QSizePolicy>
#include <QTabBar>


SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    ui = new Ui::SettingsWindow();
    ui->setupUi(this);
    setObjectName(QStringLiteral("settingsWindowFrame"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(tr("Settings"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setModal(false);
    UiAdaptive::applyAdaptiveWindowSize(this, QSize(720, 560), QSize(420, 320));

    auto *titleBar = new AppTitleBar(this, true, false, this);
    ui->titleBarHost->setFixedHeight(titleBar->height());
    ui->titleBarHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->titleBarLayout->addWidget(titleBar);
    connect(titleBar, &AppTitleBar::uiScaleChanged, this, [this, titleBar]() {
        ui->titleBarHost->setFixedHeight(titleBar->height());
    });
    ui->settingsTabs->setDocumentMode(false);
    ui->settingsTabs->tabBar()->setMinimumHeight(42);
    ui->settingsTabs->tabBar()->setExpanding(false);
#if !defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
    ui->captureBackendLabel->setVisible(false);
    ui->captureBackendHost->setVisible(false);
#endif
#if !defined(Q_OS_WIN64) && !defined(Q_OS_WIN32)
    const int serviceTabIndex = ui->settingsTabs->indexOf(ui->serviceSettingsPage);
    if (serviceTabIndex >= 0)
        ui->settingsTabs->removeTab(serviceTabIndex);
#endif
    bindUiObjects();
    populateSettingControls();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsWindow::saveSettings);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsWindow::close);
    if (QAbstractButton *closeBtn = ui->buttonBox->button(QDialogButtonBox::Close))
        connect(closeBtn, &QAbstractButton::clicked, this, &SettingsWindow::close);
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    connect(m_serviceRepairButton, &QPushButton::clicked, this, &SettingsWindow::repairWindowsService);
#endif
    connect(m_refreshAudioDevicesBtn, &QPushButton::clicked, this, &SettingsWindow::refreshAudioDevices);
    connect(m_testSpeakerBtn, &QPushButton::clicked, this, &SettingsWindow::testSpeaker);
    connect(m_testMicBtn, &QPushButton::clicked, this, &SettingsWindow::toggleMicTest);
    connect(m_notificationScriptBrowseButton, &QPushButton::clicked, this, &SettingsWindow::chooseNotificationScript);
    connect(m_notificationScriptTestButton, &QPushButton::clicked, this, &SettingsWindow::testNotificationScript);
    connect(m_thirdPartyLicensesButton, &QPushButton::clicked,
            this, &SettingsWindow::showThirdPartyLicenses);
    connect(m_aboutLatestReleaseButton, &QPushButton::clicked,
            this, &SettingsWindow::openLatestReleasePage);
    connect(m_aboutProjectButton, &QPushButton::clicked,
            this, &SettingsWindow::openProjectPage);
    connect(m_aboutStarButton, &QPushButton::clicked,
            this, &SettingsWindow::openStarPage);
    connect(m_aboutServerButton, &QPushButton::clicked,
            this, &SettingsWindow::openServerPage);
    connect(m_openh264EnableCheck, &QCheckBox::toggled, this, [this]() { refreshOpenH264Status(); });
    connect(m_openh264ImportButton, &QPushButton::clicked, this, &SettingsWindow::chooseOpenH264Binary);
    connect(m_openh264DownloadButton, &QPushButton::clicked, this, &SettingsWindow::openOpenH264ReleasePage);
    connect(m_openh264LicenseButton, &QPushButton::clicked, this, &SettingsWindow::showOpenH264License);

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    refreshServiceState();
#endif
}

SettingsWindow::~SettingsWindow()
{
    m_asyncCallbacksAlive->store(false);
    stopMicTest();
    if (m_micTestThread.joinable())
        m_micTestThread.join();
    delete ui;
}


void SettingsWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    if (!m_centeredOnFirstShow)
    {
        centerOnParent();
        m_centeredOnFirstShow = true;
    }
}


void SettingsWindow::closeEvent(QCloseEvent *event)
{
    stopMicTest();
    if (m_micTestThread.joinable())
    {
        m_closeAfterMicTest = true;
        hide();
        event->ignore();
        return;
    }
    if (m_serviceOperationRunning)
    {
        m_closeAfterServiceOperation = true;
        hide();
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}
