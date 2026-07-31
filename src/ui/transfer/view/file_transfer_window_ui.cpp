#include "ui/transfer/file_transfer_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui/chrome/app_title_bar.h"
#include "ui_file_transfer_window.h"

#include <QApplication>
#include <QPushButton>
#include <QStyle>
#include <QTableWidget>


void FileTransferWindow::initUI()
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setWindowTitle(tr("File Transfer: %1").arg(remote_id));
    UiAdaptive::applyAdaptiveWindowSize(this, QSize(900, 600), QSize(520, 360));
    ui->verticalLayout->setContentsMargins(0, 0, 0, 0);
    ui->verticalLayout->insertWidget(0, new AppTitleBar(this, true, true, this));

    dirIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    setupFileTables();
    setupLogTable();

    populateLocalFiles();
    populateRemoteFiles();

    ui->verticalLayout->setStretch(0, 0);
    ui->verticalLayout->setStretch(1, 1);
    updateLocalPathCombo();
}
