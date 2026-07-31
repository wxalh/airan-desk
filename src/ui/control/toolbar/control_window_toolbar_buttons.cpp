#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QDialog>
#include <QTableWidget>

/*
 * Creates the remote shortcut, function key, and system operation menu.
 */
QMenu *ControlWindow::createQuickActionMenu()
{
    QMenu *quickMenu = new QMenu(tr("Quick actions"), m_floatingToolbar);
    auto addKeyTap = [this](QMenu *menu, const QString &text, int winKey)
    {
        QAction *action = menu->addAction(text);
        connect(action, &QAction::triggered, this, [this, winKey]()
                { sendRemoteKeyTap(winKey); });
    };
    auto addShortcut = [this](QMenu *menu, const QString &text, QList<int> winKeys)
    {
        QAction *action = menu->addAction(text);
        connect(action, &QAction::triggered, this, [this, winKeys]()
                { sendRemoteShortcut(winKeys); });
    };
    auto addRemoteOperation = [this](QMenu *menu, const QString &text, const QString &data)
    {
        QAction *action = menu->addAction(text);
        action->setData(data);
        connect(action, &QAction::triggered, this, &ControlWindow::onRemoteOperationTriggered);
    };

    QMenu *shortcutMenu = quickMenu->addMenu(tr("Shortcuts"));
    addKeyTap(shortcutMenu, QStringLiteral("Esc"), 27);
    addKeyTap(shortcutMenu, QStringLiteral("Tab"), 9);
    addKeyTap(shortcutMenu, QStringLiteral("Win"), 91);
    addShortcut(shortcutMenu, QStringLiteral("Alt+Tab"), QList<int>{18, 9});
    addRemoteOperation(shortcutMenu, QStringLiteral("Ctrl+Alt+Del"), QStringLiteral("sas"));
    addRemoteOperation(shortcutMenu, QStringLiteral("Num Lock"), QStringLiteral("num_lock"));
    addShortcut(shortcutMenu, QStringLiteral("Ctrl+C"), QList<int>{17, 67});
    addShortcut(shortcutMenu, QStringLiteral("Ctrl+V"), QList<int>{17, 86});
    addKeyTap(shortcutMenu, QStringLiteral("F5"), 116);

    QMenu *functionMenu = quickMenu->addMenu(tr("Function keys"));
    for (int i = 1; i <= 12; ++i)
        addKeyTap(functionMenu, QStringLiteral("F%1").arg(i), 111 + i);
    functionMenu->addSeparator();
    addKeyTap(functionMenu, tr("Backspace"), 8);
    addKeyTap(functionMenu, tr("Home"), 36);
    addKeyTap(functionMenu, tr("End"), 35);
    addKeyTap(functionMenu, tr("Page Up"), 33);
    addKeyTap(functionMenu, tr("Page Down"), 34);
    addShortcut(functionMenu, QStringLiteral("Ctrl+A"), QList<int>{17, 65});
    addShortcut(functionMenu, QStringLiteral("Ctrl+X"), QList<int>{17, 88});
    addShortcut(functionMenu, QStringLiteral("Alt+F4"), QList<int>{18, 115});

    QMenu *operationMenu = quickMenu->addMenu(tr("Remote operations"));
    addRemoteOperation(operationMenu, tr("Lock"), QStringLiteral("lock"));
    addRemoteOperation(operationMenu, tr("Log off"), QStringLiteral("logoff"));
    addRemoteOperation(operationMenu, tr("Restart"), QStringLiteral("restart"));
    addRemoteOperation(operationMenu, tr("Shut down"), QStringLiteral("shutdown"));
    addRemoteOperation(operationMenu, tr("File Explorer"), QStringLiteral("resource_manager"));
    addRemoteOperation(operationMenu, tr("Task Manager"), QStringLiteral("task_manager"));

    return quickMenu;
}

/*
 * Creates the core toolbar buttons.
 */
void ControlWindow::createToolbarCoreButtons()
{
    m_screenshotBtn = new QPushButton(tr("Screenshot"), m_floatingToolbar);
    m_screenshotBtn->setToolTip(tr("Copy the current remote image to the clipboard"));
    fitControlButtonWidthToText(m_screenshotBtn);
    connect(m_screenshotBtn, &QPushButton::clicked, this, &ControlWindow::onScreenshotClicked);
    m_toolbarButtonLayout->addWidget(m_screenshotBtn);

    m_switchScreenBtn = new QToolButton(m_floatingToolbar);
    m_screenMenu = new QMenu(tr("Screens"), m_switchScreenBtn);
    m_switchScreenBtn->setText(tr("Screen"));
    m_switchScreenBtn->setToolTip(tr("Select a remote screen"));
    m_switchScreenBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_switchScreenBtn->setPopupMode(QToolButton::InstantPopup);
    m_switchScreenBtn->setMenu(m_screenMenu);
    fitControlToolButtonWidthToText(m_switchScreenBtn);
    m_toolbarButtonLayout->addWidget(m_switchScreenBtn);
    rebuildScreenMenu();

    m_remoteOperationBtn = new QToolButton(m_floatingToolbar);
    m_remoteOperationBtn->setText(tr("Quick"));
    m_remoteOperationBtn->setToolTip(tr("Send system operation commands to the remote side"));
    m_remoteOperationBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_remoteOperationBtn->setPopupMode(QToolButton::InstantPopup);
    m_remoteOperationBtn->setMenu(createQuickActionMenu());
    fitControlToolButtonWidthToText(m_remoteOperationBtn);
    m_toolbarButtonLayout->addWidget(m_remoteOperationBtn);

    m_fileTransferBtn = new QPushButton(tr("Files"), m_floatingToolbar);
    m_fileTransferBtn->setToolTip(tr("Open the file transfer window"));
    fitControlButtonWidthToText(m_fileTransferBtn);
    connect(m_fileTransferBtn, &QPushButton::clicked, this, &ControlWindow::onFileTransferClicked);
    m_toolbarButtonLayout->addWidget(m_fileTransferBtn);

    m_transferRecordBtn = new QPushButton(tr("Records"), m_floatingToolbar);
    m_transferRecordBtn->setToolTip(tr("Open transfer records"));
    fitControlButtonWidthToText(m_transferRecordBtn);
    connect(m_transferRecordBtn, &QPushButton::clicked, this, &ControlWindow::onTransferRecordClicked);
    m_toolbarButtonLayout->addWidget(m_transferRecordBtn);
}
