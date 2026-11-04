#include "settings_window.h"

#include "settings_page_layout.h"
#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_settings_window.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QShowEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QSlider>
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
    UiAdaptive::applyPreferredWindowSize(this, QSize(900, 675), QSize(520, 360));

    m_themeCombo = new QComboBox(this);
    m_themeCombo->setObjectName(QStringLiteral("settingsThemeCombo"));
    m_themeCombo->addItem(tr("Light"), 0);
    m_themeCombo->addItem(tr("Dark"), 1);
    m_themeCombo->setCurrentIndex(m_themeCombo->findData(static_cast<int>(appTheme())));
    ui->generalFormLayout->addRow(tr("Appearance"), m_themeCombo);

    auto *scaleHost = new QWidget(this);
    auto *scaleLayout = new QHBoxLayout(scaleHost);
    scaleLayout->setContentsMargins(0, 0, 0, 0);
    scaleLayout->setSpacing(UiAdaptive::scaled(12));
    m_scaleSlider = new QSlider(Qt::Horizontal, scaleHost);
    m_scaleSlider->setObjectName(QStringLiteral("settingsScaleSlider"));
    m_scaleSlider->setRange(80, 140);
    m_scaleSlider->setSingleStep(5);
    m_scaleSlider->setPageStep(10);
    m_scaleSlider->setTickInterval(10);
    m_scaleSlider->setTickPosition(QSlider::TicksBelow);
    m_scaleSlider->setValue(qBound(80, qRound(appScale() * 100.0 / 5.0) * 5, 140));
    m_scaleValue = new QLabel(scaleHost);
    m_scaleValue->setObjectName(QStringLiteral("settingsScaleValue"));
    m_scaleValue->setMinimumWidth(UiAdaptive::scaled(48));
    m_scaleValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_scaleValue->setText(tr("%1%").arg(m_scaleSlider->value()));
    scaleLayout->addWidget(m_scaleSlider, 1);
    scaleLayout->addWidget(m_scaleValue);
    ui->generalFormLayout->addRow(tr("Interface scale"), scaleHost);
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0)
            setAppTheme(static_cast<AppTheme>(m_themeCombo->itemData(index).toInt()));
    });
    connect(m_scaleSlider, &QSlider::valueChanged, this, [this](int value) {
        m_scaleValue->setText(tr("%1%").arg(value));
        setAppScale(value / 100.0);
    });

    auto *titleBar = new AppTitleBar(this, true, true, this);
    ui->titleBarHost->setFixedHeight(titleBar->height());
    ui->titleBarHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->titleBarLayout->addWidget(titleBar);
    connect(titleBar, &AppTitleBar::uiScaleChanged, this, [this, titleBar]() {
        ui->titleBarHost->setFixedHeight(titleBar->height());
    });
    ui->settingsTabs->setDocumentMode(false);
    ui->settingsTabs->tabBar()->setMinimumHeight(UiAdaptive::scaled(42));
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
    new SettingsPageLayout::Controller(ui->settingsTabs, ui->buttonBox, this);
    ui->settingsTabs->setCurrentIndex(0);
    bindUiObjects();
    populateSettingControls();
    updateResponsiveLayout();

    connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &SettingsWindow::saveSettings);
    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &SettingsWindow::reject);
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

bool SettingsWindow::event(QEvent *event)
{
    const bool handled = QDialog::event(event);
    if (event && (event->type() == QEvent::StyleChange || event->type() == QEvent::ApplicationFontChange))
        QTimer::singleShot(0, this, [this]() { updateResponsiveLayout(); });
    return handled;
}


void SettingsWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    m_openingTheme = appTheme();
    m_openingScale = appScale();
    m_appearanceSnapshotValid = true;
    if (!m_centeredOnFirstShow)
    {
        centerOnParent();
        m_centeredOnFirstShow = true;
    }
}


void SettingsWindow::reject()
{
    stopMicTest();
    if (m_micTestThread.joinable())
    {
        m_closeAfterMicTest = true;
        hide();
        return;
    }
    if (m_serviceOperationRunning)
    {
        m_closeAfterServiceOperation = true;
        hide();
        return;
    }
    restoreAppearancePreview();
    QDialog::reject();
}


void SettingsWindow::restoreAppearancePreview()
{
    if (!m_appearanceSnapshotValid)
        return;
    setAppTheme(m_openingTheme);
    setAppScale(m_openingScale);
    if (m_themeCombo)
    {
        const QSignalBlocker blocker(m_themeCombo);
        m_themeCombo->setCurrentIndex(m_themeCombo->findData(static_cast<int>(m_openingTheme)));
    }
    if (m_scaleSlider)
    {
        const QSignalBlocker blocker(m_scaleSlider);
        m_scaleSlider->setValue(qBound(80, qRound(m_openingScale * 100.0 / 5.0) * 5, 140));
    }
    if (m_scaleValue && m_scaleSlider)
        m_scaleValue->setText(tr("%1%").arg(m_scaleSlider->value()));
    m_appearanceSnapshotValid = false;
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
    restoreAppearancePreview();
    QDialog::closeEvent(event);
}


void SettingsWindow::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateResponsiveLayout();
}


void SettingsWindow::updateResponsiveLayout()
{
    if (!ui)
        return;
    SettingsPageLayout::apply(ui->settingsTabs, ui->buttonBox);
}
