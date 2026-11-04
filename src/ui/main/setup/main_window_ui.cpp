#include "ui/main/main_window.h"
#include "common/constant.h"
#include "ui/chrome/app_title_bar.h"
#include "ui/common/adaptive_ui.h"
#include "ui/main/layout/main_window_home_layout.h"
#include "ui/widgets/password/password_line_edit_util.h"
#include "ui_main_window.h"
#include <QApplication>
#include <QGridLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QCoreApplication>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#include <wtsapi32.h>
#endif

void MainWindow::initUI()
{
    ui = new Ui::MainWindow();
    ui->setupUi(this);
    setObjectName(QStringLiteral("MainWindow"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(windowTitle);
    setWindowIcon(qApp->windowIcon());
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    if (ui->rootLayout)
        ui->rootLayout->setContentsMargins(0, 0, 0, 0);

    m_titleBar = new AppTitleBar(this, true, true, this);
    if (ui->titleBarLayout)
        ui->titleBarLayout->addWidget(m_titleBar);

    m_content = ui->mainContent;
    bindUiObjects();
    applyMainScale();

    m_localIdEdit->setPlainText(ConfigUtil->local_id);
    m_localPwdEdit->setPlainText(ConfigUtil->getLocalPwd());
    m_localIdEdit->setReadOnly(true);
    m_localPwdEdit->setReadOnly(true);

    auto *escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escapeShortcut->setContext(Qt::WindowShortcut);
    connect(escapeShortcut, &QShortcut::activated, this, [this]()
            {
        if (m_trayIcon && m_trayIcon->isVisible())
        {
            hide();
            LOG_INFO("Main window hidden to system tray by Escape");
        } });

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    WTSRegisterSessionNotification(reinterpret_cast<HWND>(winId()), NOTIFY_FOR_THIS_SESSION);
#endif
}


void MainWindow::bindUiObjects()
{
    m_allowControlLabel = ui->allow_control_label;
    m_localIdLabel = ui->local_id_label;
    m_localPwdLabel = ui->local_pwd_label;
    m_remoteControlLabel = ui->remote_control_label;
    m_remoteIdLabel = ui->remote_id_label;
    m_remotePwdLabel = ui->remote_pwd_label;
    m_wsConnectStatus = ui->ws_connect_status;
    m_versionLabel = new QLabel(tr("Version: %1").arg(QCoreApplication::applicationVersion()), m_content);
    m_versionLabel->setObjectName(QStringLiteral("mainVersionLabel"));
    m_versionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_localIdEdit = ui->local_id;
    m_localPwdEdit = ui->local_pwd;
    m_remoteIdEdit = ui->remote_id;
    m_remotePwdEdit = ui->remote_pwd;
    m_remoteDesktopRadio = ui->remote_desktop;
    m_remoteFileRadio = ui->remote_file;
    m_remoteTerminalRadio = ui->remote_terminal;
    m_connectButton = ui->btn_conn;
    m_localPwdChangeButton = ui->local_pwd_change;
    m_localShareButton = ui->local_share;
    m_settingsButton = ui->mainSettingsButton;
    m_settingsButton->setToolTip(tr("Settings"));
    m_settingsButton->setAccessibleName(tr("Settings"));
    m_settingsButton->setObjectName(QStringLiteral("mainSettingsButton"));
    m_myDevicesButton = new QPushButton(tr("My devices"), m_content);
    m_myDevicesButton->setObjectName(QStringLiteral("myDevicesButton"));
    connect(m_myDevicesButton, &QPushButton::clicked, this, &MainWindow::openMyDevices);

    m_localIdEdit->setReadOnly(true);
    m_localPwdEdit->setReadOnly(true);
    m_remoteIdEdit->setClearButtonEnabled(true);
    installPasswordRevealButton(m_remotePwdEdit, true);
    connect(m_remoteIdEdit, &QLineEdit::textChanged, this, [this](const QString &text)
            { tryFillRemoteFieldsFromShareText(text); });
    connect(m_remoteIdEdit, &QLineEdit::editingFinished, this, [this]()
            {
                const QString text = m_remoteIdEdit->text().trimmed();
                if (m_remoteIdEdit->text() != text)
                    m_remoteIdEdit->setText(text);
            });
    connect(m_remotePwdEdit, &QLineEdit::editingFinished, this, [this]()
            {
                const QString text = m_remotePwdEdit->text().trimmed();
                if (m_remotePwdEdit->text() != text)
                    m_remotePwdEdit->setText(text);
            });

    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::openSettingsFromTray);
    m_remoteDesktopRadio->setChecked(true);

    buildMainContentLayout();
}

void MainWindow::buildMainContentLayout()
{
    if (!m_content)
        return;

    HomePageLayout::build(m_content, {
        m_allowControlLabel, m_localIdLabel, m_localPwdLabel,
        m_remoteControlLabel, m_remoteIdLabel, m_remotePwdLabel,
        m_wsConnectStatus, m_versionLabel,
        m_localIdEdit, m_localPwdEdit, m_remoteIdEdit, m_remotePwdEdit,
        m_remoteDesktopRadio, m_remoteFileRadio, m_remoteTerminalRadio,
        m_connectButton, m_localShareButton, m_localPwdChangeButton,
        m_myDevicesButton, m_settingsButton});
    m_mainPanelGrid = m_content->findChild<QGridLayout *>(QStringLiteral("mainPanelGrid"));
    m_localPanel = m_content->findChild<QWidget *>(QStringLiteral("localDevicePanel"));
    m_remotePanel = m_content->findChild<QWidget *>(QStringLiteral("remoteDevicePanel"));
}
