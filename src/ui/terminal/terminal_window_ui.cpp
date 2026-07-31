#include "terminal_window.h"

#include "terminal/emulator/native_terminal_widget.h"
#include "terminal/file_panel/terminal_file_panel.h"
#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_terminal_window.h"
#include "util/json/json_util.h"

#include <QCheckBox>
#include <QLabel>
#include <QSizePolicy>


void TerminalWindow::initUI()
{
    ui = new Ui::TerminalWindow();
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(tr("Terminal: %1").arg(m_remoteId));
    const QSize windowSize = UiAdaptive::applyAdaptiveWindowSize(this, QSize(980, 620), QSize(560, 360));

    auto *titleBar = new AppTitleBar(this, true, true, this);
    ui->titleBarHost->setFixedHeight(titleBar->height());
    ui->titleBarHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(titleBar, &AppTitleBar::uiScaleChanged, this, [this, titleBar]() {
        ui->titleBarHost->setFixedHeight(titleBar->height());
    });
    ui->terminalToolsWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->rootLayout->setStretch(0, 0);
    ui->rootLayout->setStretch(1, 0);
    ui->rootLayout->setStretch(2, 1);
    ui->titleBarLayout->addWidget(titleBar);
    m_autoSaveLogCheck = ui->autoSaveLogCheck;
    m_autoSaveLogPathLabel = ui->autoSaveLogPathLabel;
    m_autoSaveLogPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_filePanel = new TerminalFilePanel(ui->terminalSplitter);
    m_terminal = new NativeTerminalWidget(ui->terminalSplitter);
    ui->terminalSplitter->addWidget(m_filePanel);
    ui->terminalSplitter->addWidget(m_terminal);
    ui->terminalSplitter->setStretchFactor(0, 0);
    ui->terminalSplitter->setStretchFactor(1, 1);
    ui->terminalSplitter->setSizes({qMax(220, windowSize.width() / 3), qMax(420, windowSize.width() * 2 / 3)});

    connect(m_terminal, &NativeTerminalWidget::inputGenerated, this, &TerminalWindow::sendTerminalInput);
    connect(m_terminal, &NativeTerminalWidget::gridSizeChanged, this, &TerminalWindow::sendTerminalResize);
    connect(m_terminal, &NativeTerminalWidget::currentDirectoryChanged, m_filePanel, &TerminalFilePanel::followTerminalPath);
    connect(m_filePanel, &TerminalFilePanel::requestFileList, this, &TerminalWindow::requestFileList);
    connect(m_filePanel, &TerminalFilePanel::requestDownload, this, &TerminalWindow::requestDownload);
    connect(m_filePanel, &TerminalFilePanel::requestUpload, this, &TerminalWindow::requestUpload);
    connect(m_filePanel, &TerminalFilePanel::requestRemoteOperation, this, [this](const QJsonObject &message) {
        emit filePanelTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(message).toStdString()));
    });
    connect(m_filePanel, &TerminalFilePanel::requestRemoteDrag, this, [this](const QJsonArray &files, const QString &requestId, QString *errorMessage) -> bool {
        return m_rtcCtl.startRemoteFileDrag(m_filePanel, files, requestId, errorMessage);
    });
    connect(m_terminal, &NativeTerminalWidget::terminalTitleChanged, this, &TerminalWindow::updateTerminalTitle);
    connect(m_terminal, &NativeTerminalWidget::screenCleared, this, [this]() {
        appendTerminalLogMarker(tr("Screen cleared"));
    });
    connect(m_autoSaveLogCheck, &QCheckBox::toggled, this, &TerminalWindow::onAutoSaveLogToggled);
    m_terminal->showStatusLine(tr("Connecting to remote terminal..."));
}


void TerminalWindow::updateTerminalTitle(const QString &title)
{
    if (title.isEmpty())
        setWindowTitle(tr("Terminal: %1").arg(m_remoteId));
    else
        setWindowTitle(tr("%1 - %2").arg(title, m_remoteId));
}
