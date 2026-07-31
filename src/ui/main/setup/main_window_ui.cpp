#include "ui/main/main_window.h"
#include "common/constant.h"
#include "ui/chrome/app_title_bar.h"
#include "ui/widgets/password/password_line_edit_util.h"
#include "ui_main_window.h"
#include <QApplication>
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

namespace
{
    
    QIcon makeSettingsIcon()
    {
        QPixmap pixmap(32, 32);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.translate(16, 16);

        QPen pen(QColor(131, 193, 224), 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        for (int i = 0; i < 8; ++i)
        {
            painter.save();
            painter.rotate(i * 45.0);
            painter.drawLine(QPointF(0, -12), QPointF(0, -9));
            painter.restore();
        }

        painter.drawEllipse(QPointF(0, 0), 9.0, 9.0);
        painter.drawEllipse(QPointF(0, 0), 3.2, 3.2);
        return QIcon(pixmap);
    }

}


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
        ui->rootLayout->setContentsMargins(2, 2, 2, 2);

    m_titleBar = new AppTitleBar(this, true, false, this);
    m_titleBar->setResizeAspectRatio(QSize(800, 638));
    if (ui->titleBarLayout)
        ui->titleBarLayout->addWidget(m_titleBar);

    m_content = ui->mainContent;
    bindUiObjects();
    applyMainScale();

    m_localIdEdit->setText(ConfigUtil->local_id);
    m_localPwdEdit->setText(ConfigUtil->getLocalPwd());
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
    m_versionLabel->setStyleSheet(QStringLiteral("color: rgba(131, 193, 224, 150);"));
    m_localIdEdit = ui->local_id;
    m_localPwdEdit = ui->local_pwd;
    m_remoteIdEdit = ui->remote_id;
    m_remotePwdEdit = ui->remote_pwd;
    m_localIdBorder = ui->local_id_border;
    m_localPwdBorder = ui->local_pwd_border;
    m_remoteIdBorder = ui->remote_id_border;
    m_remotePwdBorder = ui->remote_pwd_border;
    m_remoteDesktopRadio = ui->remote_desktop;
    m_remoteFileRadio = ui->remote_file;
    m_remoteTerminalRadio = ui->remote_terminal;
    m_connectButton = ui->btn_conn;
    m_localPwdChangeButton = ui->local_pwd_change;
    m_localShareButton = ui->local_share;
    m_settingsButton = ui->mainSettingsButton;
    m_connectDivider = new QWidget(m_content);
    m_localPwdChangeDivider = new QWidget(m_content);
    m_localShareDivider = new QWidget(m_content);
    for (QWidget *divider : {m_connectDivider, m_localPwdChangeDivider, m_localShareDivider})
    {
        divider->setObjectName(QStringLiteral("inputActionDivider"));
        divider->setStyleSheet(QStringLiteral("background-color: #565656; border: 0;"));
    }
    m_settingsButton->setText(QString());
    m_settingsButton->setIcon(makeSettingsIcon());
    m_settingsButton->setIconSize(QSize(20, 20));
    m_settingsButton->setToolTip(tr("Settings"));
    m_settingsButton->setAccessibleName(tr("Settings"));

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

    for (QWidget *border : {m_localPwdBorder, m_localIdBorder, m_remotePwdBorder, m_remoteIdBorder})
        border->lower();
}
