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
    {
        QVector<int> stretchCols;
        stretchCols.append(0);
        QVector<QPair<int, int>> fixedCols;
        fixedCols.append(qMakePair(1, 90));
        fixedCols.append(qMakePair(2, 150));
        fixedCols.append(qMakePair(3, 90));
        UiAdaptive::makeTableAdaptive(ui->localTable, stretchCols, fixedCols, true);
    }
    ui->localTable->setStyleSheet(QStringLiteral("QTableCornerButton::section { background-color: #202020; border: 1px solid #3a3a3a; }"));
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
    {
        QVector<int> stretchCols;
        stretchCols.append(0);
        QVector<QPair<int, int>> fixedCols;
        fixedCols.append(qMakePair(1, 90));
        fixedCols.append(qMakePair(2, 150));
        fixedCols.append(qMakePair(3, 90));
        UiAdaptive::makeTableAdaptive(ui->remoteTable, stretchCols, fixedCols, true);
    }
    ui->remoteTable->setStyleSheet(QStringLiteral("QTableCornerButton::section { background-color: #202020; border: 1px solid #3a3a3a; }"));
    connect(ui->remoteTable, &QTableWidget::customContextMenuRequested,
            this, &FileTransferWindow::onRemoteTableContextMenuRequested);

    ui->localTable->viewport()->installEventFilter(this);
    ui->remoteTable->viewport()->installEventFilter(this);
}


void FileTransferWindow::setupLogTable()
{
    ui->transferLogTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->transferLogTable->setAlternatingRowColors(false);
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
    ui->transferLogTable->setStyleSheet(QStringLiteral(
        "QTableWidget#transferLogTable {"
        "    background-color: #181818;"
        "    alternate-background-color: #181818;"
        "    color: rgb(131, 193, 224);"
        "    gridline-color: #3a3a3a;"
        "    border: 1px solid #3a3a3a;"
        "    selection-background-color: #783041;"
        "}"
        "QTableWidget#transferLogTable::item {"
        "    background-color: #181818;"
        "    color: rgb(131, 193, 224);"
        "    padding: 4px;"
        "}"
        "QTableWidget#transferLogTable::item:selected {"
        "    background-color: #783041;"
        "    color: #ffffff;"
        "}"
        "QTableWidget#transferLogTable QHeaderView::section {"
        "    background-color: #202020;"
        "    color: rgb(131, 193, 224);"
        "    border: 1px solid #3a3a3a;"
        "    padding: 5px;"
        "}"
        "QTableWidget#transferLogTable QTableCornerButton::section {"
        "    background-color: #202020;"
        "    border: 1px solid #3a3a3a;"
        "}"
        "QTableWidget#transferLogTable QLabel#transferProgressLabel {"
        "    background: transparent;"
        "    color: rgb(131, 193, 224);"
        "    padding: 0 6px;"
        "}"));

    ui->transferLogTable->setColumnCount(5);
    ui->transferLogTable->setHorizontalHeaderLabels({tr("Source path"), tr("Target path"), tr("Progress"), tr("Status"), tr("Action")});
}
