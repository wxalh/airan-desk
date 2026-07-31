#include "terminal/file_panel/terminal_file_panel.h"

#include <QComboBox>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>


void TerminalFilePanel::onPathEditingFinished()
{
    const QString path = m_remotePathEdit->text().trimmed();
    if (m_connected && !path.isEmpty() && path != m_currentRemotePath)
        setRemotePath(path);
}


void TerminalFilePanel::onParentClicked()
{
    if (m_connected && !m_currentRemotePath.isEmpty())
        setRemotePath(parentRemotePath(m_currentRemotePath));
}


void TerminalFilePanel::onRefreshClicked()
{
    if (m_connected && !m_currentRemotePath.isEmpty())
        emit requestFileList(m_currentRemotePath);
}


void TerminalFilePanel::onDriveChanged(int index)
{
    if (m_updatingDriveCombo || !m_connected || index < 0 || !m_driveCombo)
        return;

    const QString path = m_driveCombo->itemData(index).toString();
    if (!path.isEmpty())
        setRemotePath(path);
}


void TerminalFilePanel::onRemoteCellDoubleClicked(int row, int)
{
    QTableWidgetItem *item = row >= 0 ? m_remoteTable->item(row, 0) : nullptr;
    if (!item || !m_connected || m_currentRemotePath.isEmpty())
        return;

    const bool isDir = item->data(Qt::UserRole).toBool();
    if (!isDir)
        return;

    const QString nextPath = item->text() == QStringLiteral("..")
                                 ? parentRemotePath(m_currentRemotePath)
                                 : joinRemotePath(m_currentRemotePath, item->text());
    setRemotePath(nextPath);
}
