#include "terminal/file_panel/terminal_file_panel.h"
#include "terminal/file_panel/view/terminal_toolbar_flow_layout.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QList>
#include <QPixmap>
#include <QSize>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>

namespace
{

QIcon rotatedIcon(const QIcon &sourceIcon, const QSize &size, qreal degrees)
{
    const QPixmap sourcePixmap = sourceIcon.pixmap(size);
    if (sourcePixmap.isNull())
        return sourceIcon;

    return QIcon(sourcePixmap.transformed(QTransform().rotate(degrees), Qt::SmoothTransformation));
}
} // namespace


void TerminalFilePanel::createToolbar(QVBoxLayout *layout)
{
    auto *toolbar = new QVBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(scaled(3));
    auto *actions = new TerminalToolbarFlowLayout(scaled(3));
    actions->setObjectName(QStringLiteral("terminalFileActionsLayout"));
    auto *deviceOptionsRow = new QHBoxLayout();
    deviceOptionsRow->setContentsMargins(0, 0, 0, 0);
    deviceOptionsRow->setSpacing(scaled(3));

    m_parentButton = new QToolButton(this);
    m_parentButton->setToolTip(tr("Parent directory"));

    m_refreshButton = new QToolButton(this);
    m_refreshButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh"));

    m_downloadButton = new QToolButton(this);
    m_downloadButton->setToolTip(tr("Download"));

    m_uploadFileButton = new QToolButton(this);
    m_uploadFileButton->setToolTip(tr("Upload file"));

    m_shutdownButton = new QToolButton(this);
    m_shutdownButton->setObjectName(QStringLiteral("terminalShutdownButton"));
    m_shutdownButton->setIcon(QIcon(QStringLiteral(":/icons/shutdown.svg")));
    m_shutdownButton->setToolTip(tr("Shut down remote device"));
    m_shutdownButton->setAccessibleName(m_shutdownButton->toolTip());

    m_restartButton = new QToolButton(this);
    m_restartButton->setObjectName(QStringLiteral("terminalRestartButton"));
    m_restartButton->setIcon(QIcon(QStringLiteral(":/icons/restart.svg")));
    m_restartButton->setToolTip(tr("Restart remote device"));
    m_restartButton->setAccessibleName(m_restartButton->toolTip());

    m_uploadDirectoryButton = new QToolButton(this);
    m_uploadDirectoryButton->setToolTip(tr("Upload directory"));

    m_parentButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_ArrowLeft));
    const QIcon downloadIcon = QApplication::style()->standardIcon(QStyle::SP_ArrowDown);
    const QIcon uploadIcon = rotatedIcon(downloadIcon, QSize(scaled(14), scaled(14)), 180);
    m_downloadButton->setIcon(downloadIcon);
    m_uploadFileButton->setIcon(uploadIcon);
    m_uploadDirectoryButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));

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
        button->setText(QString());
        button->setMinimumSize(scaled(28), scaled(28));
        button->setMaximumSize(scaled(36), scaled(36));
        button->setIconSize(QSize(scaled(14), scaled(14)));
        button->setAutoRaise(true);
        actions->addWidget(button);
    }

    m_driveCombo = new QComboBox(this);
    m_driveCombo->setToolTip(tr("Drive"));
    m_driveCombo->setMinimumSize(scaled(58), scaled(28));
    m_driveCombo->setMaximumHeight(scaled(36));
    m_driveCombo->setMinimumContentsLength(3);
    m_driveCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_driveCombo->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_followPathCheck = new QCheckBox(tr("Follow terminal"), this);
    m_followPathCheck->setChecked(true);
    m_followPathCheck->setToolTip(tr("Follow terminal path"));
    m_followPathCheck->setMinimumHeight(scaled(28));
    m_followPathCheck->setMaximumHeight(scaled(36));

    deviceOptionsRow->addWidget(m_driveCombo);
    deviceOptionsRow->addWidget(m_followPathCheck, 1);
    toolbar->addLayout(actions);
    toolbar->addLayout(deviceOptionsRow);
    layout->addLayout(toolbar);
}
