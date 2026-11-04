#include "ui/transfer/file_transfer_window.h"

#include "ui/common/adaptive_ui.h"
#include "ui_file_transfer_window.h"

#include <QApplication>
#include <QHeaderView>
#include <QMimeData>
#include <QDrag>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMouseEvent>
#include <QTableWidget>


void FileTransferWindow::setupFileTables()
{
    ui->localTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->localTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->localTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->localTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->localTable->setAcceptDrops(true);
    ui->localTable->viewport()->setAcceptDrops(true);
    ui->localTable->setDragEnabled(true);
    ui->localTable->setDragDropMode(QAbstractItemView::DragDrop);
    ui->localTable->setAlternatingRowColors(true);
    ui->localTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    {
        QVector<int> stretchCols;
        stretchCols.append(0);
        QVector<QPair<int, int>> fixedCols;
        fixedCols.append(qMakePair(1, 90));
        fixedCols.append(qMakePair(2, 150));
        fixedCols.append(qMakePair(3, 90));
        UiAdaptive::makeTableAdaptive(ui->localTable, stretchCols, fixedCols, true);
    }
    connect(ui->localTable, &QTableWidget::customContextMenuRequested,
            this, &FileTransferWindow::onLocalTableContextMenuRequested);

    ui->remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->remoteTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->remoteTable->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->remoteTable->setAcceptDrops(true);
    ui->remoteTable->viewport()->setAcceptDrops(true);
    ui->remoteTable->setDragEnabled(true);
    ui->remoteTable->setDragDropMode(QAbstractItemView::DragDrop);
    ui->remoteTable->setAlternatingRowColors(true);
    ui->remoteTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    {
        QVector<int> stretchCols;
        stretchCols.append(0);
        QVector<QPair<int, int>> fixedCols;
        fixedCols.append(qMakePair(1, 90));
        fixedCols.append(qMakePair(2, 150));
        fixedCols.append(qMakePair(3, 90));
        UiAdaptive::makeTableAdaptive(ui->remoteTable, stretchCols, fixedCols, true);
    }
    connect(ui->remoteTable, &QTableWidget::customContextMenuRequested,
            this, &FileTransferWindow::onRemoteTableContextMenuRequested);

    ui->localTable->viewport()->installEventFilter(this);
    ui->remoteTable->viewport()->installEventFilter(this);
}


void FileTransferWindow::setupLogTable()
{
    ui->transferLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->transferLogTable->setAlternatingRowColors(true);
    ui->transferLogTable->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->transferLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    {
        QVector<int> stretchCols;
        stretchCols.append(0);
        stretchCols.append(1);
        QVector<QPair<int, int>> fixedCols;
        fixedCols.append(qMakePair(2, 210));
        fixedCols.append(qMakePair(3, 90));
        fixedCols.append(qMakePair(4, 90));
        UiAdaptive::makeTableAdaptive(ui->transferLogTable, stretchCols, fixedCols, false);
    }
    ui->transferLogTable->verticalHeader()->setVisible(false);
    ui->transferLogTable->verticalHeader()->setDefaultSectionSize(30);
    ui->transferLogTable->setColumnCount(5);
    ui->transferLogTable->setHorizontalHeaderLabels({tr("Source path"), tr("Target path"), tr("Progress"), tr("Status"), tr("Action")});
}
