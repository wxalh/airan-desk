#include "terminal/file_panel/terminal_file_panel.h"

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
    auto *toolbar = new QHBoxLayout();
    toolbar->setContentsMargins(0, 0, 0, 0);
    toolbar->setSpacing(scaled(2));

    m_parentButton = new QToolButton(this);
    m_parentButton->setToolTip(tr("Parent directory"));

    m_refreshButton = new QToolButton(this);
    m_refreshButton->setIcon(QApplication::style()->standardIcon(QStyle::SP_BrowserReload));
    m_refreshButton->setToolTip(tr("Refresh"));

    m_downloadButton = new QToolButton(this);
    m_downloadButton->setToolTip(tr("Download"));

    m_uploadFileButton = new QToolButton(this);
    m_uploadFileButton->setToolTip(tr("Upload file"));

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
        m_uploadDirectoryButton,
    };
    for (QToolButton *button : toolButtons)
    {
        button->setText(QString());
        button->setFixedSize(scaled(24), scaled(22));
        button->setIconSize(QSize(scaled(14), scaled(14)));
        button->setAutoRaise(true);
    }

    m_driveCombo = new QComboBox(this);
    m_driveCombo->setToolTip(tr("Drive"));
    m_driveCombo->setFixedSize(scaled(58), scaled(22));
    m_driveCombo->setMinimumContentsLength(3);
    m_driveCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_driveCombo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    m_followPathCheck = new QCheckBox(tr("Follow terminal"), this);
    m_followPathCheck->setChecked(true);
    m_followPathCheck->setToolTip(tr("Follow terminal path"));
    m_followPathCheck->setFixedHeight(scaled(22));

    toolbar->addWidget(m_parentButton);
    toolbar->addWidget(m_refreshButton);
    toolbar->addWidget(m_downloadButton);
    toolbar->addWidget(m_uploadFileButton);
    toolbar->addWidget(m_uploadDirectoryButton);
    toolbar->addWidget(m_driveCombo);
    toolbar->addWidget(m_followPathCheck);
    toolbar->addStretch();
    layout->addLayout(toolbar);
}
