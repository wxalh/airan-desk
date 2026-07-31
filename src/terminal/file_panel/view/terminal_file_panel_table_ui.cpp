#include "terminal/file_panel/terminal_file_panel.h"

#include "ui/common/adaptive_ui.h"

#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QTableWidget>
#include <QVBoxLayout>


void TerminalFilePanel::createPathEdit(QVBoxLayout *layout)
{
    m_remotePathEdit = new QLineEdit(this);
    m_remotePathEdit->setPlaceholderText(tr("Remote path"));
    m_remotePathEdit->setFixedHeight(scaled(24));
    layout->addWidget(m_remotePathEdit);
}


void TerminalFilePanel::createRemoteTable(QVBoxLayout *layout)
{
    m_remoteTable = new QTableWidget(this);
    m_remoteTable->setColumnCount(3);
    m_remoteTable->setHorizontalHeaderLabels({tr("Name"), tr("Size"), tr("Modified")});
    m_remoteTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_remoteTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_remoteTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_remoteTable->setShowGrid(false);
    m_remoteTable->verticalHeader()->setVisible(false);
    m_remoteTable->horizontalHeader()->setSectionsMovable(false);

    QVector<int> stretchCols;
    stretchCols.append(0);
    QVector<QPair<int, int>> fixedCols;
    fixedCols.append(qMakePair(1, scaled(72)));
    fixedCols.append(qMakePair(2, scaled(132)));
    UiAdaptive::makeTableAdaptive(m_remoteTable, stretchCols, fixedCols, false);

    m_remoteTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_remoteTable->verticalHeader()->setDefaultSectionSize(scaled(27));
    m_remoteTable->setAlternatingRowColors(false);
    layout->addWidget(m_remoteTable, 1);
}


void TerminalFilePanel::createTransferStatus(QVBoxLayout *layout)
{
    m_transferStatusLabel = new QLabel(tr("Transfer idle"), this);
    m_transferStatusLabel->setObjectName(QStringLiteral("terminalTransferStatusLabel"));
    m_transferStatusLabel->setMinimumHeight(scaled(18));
    m_transferStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_transferProgressBar = new QProgressBar(this);
    m_transferProgressBar->setObjectName(QStringLiteral("terminalTransferProgressBar"));
    m_transferProgressBar->setRange(0, 100);
    m_transferProgressBar->setValue(0);
    m_transferProgressBar->setTextVisible(false);
    m_transferProgressBar->setFixedHeight(scaled(8));

    layout->addWidget(m_transferStatusLabel);
    layout->addWidget(m_transferProgressBar);
}
