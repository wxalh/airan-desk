#include "terminal/file_panel/terminal_file_panel.h"
#include "ui/common/adaptive_ui.h"

#include <QComboBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QProgressBar>
#include <QScreen>
#include <QSize>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QCheckBox>


int TerminalFilePanel::scaled(int value) const
{
    QScreen *screen = nullptr;
    if (window() && window()->windowHandle())
        screen = window()->windowHandle()->screen();
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    const double scale = screen ? qBound(0.75, screen->logicalDotsPerInch() / 96.0, 3.0) : 1.0;
    return qMax(1, static_cast<int>(qRound(value * scale * UiAdaptive::scaleFactor())));
}


bool TerminalFilePanel::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (!event)
        return handled;

    switch (event->type())
    {
    case QEvent::StyleChange:
        // The application stylesheet repolishes child widgets after their
        // parent's StyleChange. Restore the scaled action metrics afterwards.
        QTimer::singleShot(0, this, [this]() { refreshDpiMetrics(); });
        break;
    case QEvent::ApplicationFontChange:
    case QEvent::ScreenChangeInternal:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    case QEvent::DevicePixelRatioChange:
#endif
        refreshDpiMetrics();
        break;
    default:
        break;
    }
    return handled;
}


void TerminalFilePanel::setupUi()
{
    setMinimumWidth(scaled(170));
    setObjectName(QStringLiteral("terminalFilePanel"));
    applyPanelStyle();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(scaled(3), scaled(3), scaled(3), scaled(3));
    layout->setSpacing(scaled(2));

    createToolbar(layout);
    createPathEdit(layout);
    createRemoteTable(layout);
    createTransferStatus(layout);
    connectUiSignals();

    setConnected(false);
    updatePathEdit();
}


void TerminalFilePanel::refreshDpiMetrics()
{
    setMinimumWidth(scaled(170));
    if (layout())
    {
        layout()->setContentsMargins(scaled(3), scaled(3), scaled(3), scaled(3));
        layout()->setSpacing(scaled(2));
    }

    const QList<QToolButton *> toolButtons = {
        m_parentButton,
        m_refreshButton,
        m_downloadButton,
        m_uploadFileButton,
        m_shutdownButton,
        m_restartButton,
        m_uploadDirectoryButton,
    };
    for (QToolButton *button : toolButtons)
    {
        if (!button)
            continue;
        button->setMinimumSize(scaled(28), scaled(28));
        button->setMaximumSize(scaled(36), scaled(36));
        button->setIconSize(QSize(scaled(14), scaled(14)));
    }
    if (QLayout *actions = findChild<QLayout *>(QStringLiteral("terminalFileActionsLayout")))
        actions->setSpacing(scaled(3));
    if (m_driveCombo)
    {
        m_driveCombo->setMinimumSize(scaled(58), scaled(28));
        m_driveCombo->setMaximumHeight(scaled(36));
    }
    if (m_followPathCheck)
    {
        m_followPathCheck->setMinimumHeight(scaled(28));
        m_followPathCheck->setMaximumHeight(scaled(36));
    }
    if (m_remotePathEdit)
    {
        m_remotePathEdit->setMinimumHeight(scaled(32));
        m_remotePathEdit->setMaximumHeight(scaled(40));
    }
    if (m_remoteTable)
    {
        m_remoteTable->setColumnWidth(1, scaled(72));
        m_remoteTable->setColumnWidth(2, scaled(132));
        m_remoteTable->verticalHeader()->setDefaultSectionSize(scaled(27));
    }
    if (m_transferStatusLabel)
        m_transferStatusLabel->setMinimumHeight(scaled(18));
    if (m_transferProgressBar)
        m_transferProgressBar->setFixedHeight(scaled(8));
    updateGeometry();
}
