#include "ui/transfer/file_transfer_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_file_transfer_window.h"

#include <QApplication>
#include <QPushButton>
#include <QStyle>
#include <QSplitter>
#include <QSizePolicy>
#include <QTableWidget>


void FileTransferWindow::initUI()
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(tr("File Transfer: %1").arg(remote_id));
    UiAdaptive::applyPreferredWindowSize(this, QSize(1040, 780), QSize(460, 320));
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->insertWidget(0, new AppTitleBar(this, true, true, this));

    m_devicePanes = new QSplitter(Qt::Horizontal, this);
    m_devicePanes->setObjectName(QStringLiteral("fileTransferPanes"));
    m_devicePanes->setChildrenCollapsible(false);
    m_devicePanes->setHandleWidth(UiAdaptive::scaled(6));
    m_devicePanes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->horizontalLayout->removeWidget(ui->localGroupBox);
    ui->horizontalLayout->removeWidget(ui->remoteGroupBox);
    ui->localGroupBox->setMinimumWidth(0);
    ui->remoteGroupBox->setMinimumWidth(0);
    ui->localGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->remoteGroupBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_devicePanes->addWidget(ui->localGroupBox);
    m_devicePanes->addWidget(ui->remoteGroupBox);
    m_devicePanes->setStretchFactor(0, 1);
    m_devicePanes->setStretchFactor(1, 1);
    ui->horizontalLayout->addWidget(m_devicePanes);

    dirIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    setupFileTables();
    setupLogTable();
    updateResponsivePaneLayout();

    populateLocalFiles();
    populateRemoteFiles();

    ui->verticalLayout->setStretch(0, 0);
    ui->verticalLayout->setStretch(1, 3);
    ui->verticalLayout->setStretch(4, 2);
    ui->transferLogTable->setMinimumHeight(100);
    updateLocalPathCombo();
}


void FileTransferWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateResponsivePaneLayout();
}


void FileTransferWindow::updateResponsivePaneLayout()
{
    if (!m_devicePanes)
        return;
    const Qt::Orientation orientation = width() < UiAdaptive::scaled(840) ? Qt::Vertical : Qt::Horizontal;
    if (orientation == m_devicePanesOrientation)
        return;

    m_devicePanesOrientation = orientation;
    m_devicePanes->setOrientation(orientation);
    const int extent = orientation == Qt::Vertical
                           ? qMax(m_devicePanes->height(), height())
                           : qMax(m_devicePanes->width(), width());
    if (orientation == Qt::Vertical)
        m_devicePanes->setSizes({extent * 3 / 5, extent * 2 / 5});
    else
        m_devicePanes->setSizes({extent / 2, extent - extent / 2});
}
