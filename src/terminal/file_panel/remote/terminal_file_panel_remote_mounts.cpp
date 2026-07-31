#include "terminal/file_panel/terminal_file_panel.h"

#include <QComboBox>
#include <QDir>
#include <QLineEdit>


void TerminalFilePanel::updatePathEdit()
{
    if (m_remotePathEdit->text() != m_currentRemotePath)
        m_remotePathEdit->setText(m_currentRemotePath);
}


void TerminalFilePanel::updateMountedPaths(const QJsonArray &paths)
{
    QStringList mountedPaths;
    for (const QJsonValue &value : paths)
    {
        const QString path = value.toString();
        if (!path.isEmpty() && !mountedPaths.contains(path))
            mountedPaths.append(path);
    }

    if (mountedPaths != m_mountedPaths)
    {
        m_mountedPaths = mountedPaths;
        updateDriveCombo();
    }
}


void TerminalFilePanel::updateDriveCombo()
{
    if (!m_driveCombo)
        return;

    m_updatingDriveCombo = true;
    m_driveCombo->clear();
    for (const QString &path : m_mountedPaths)
        m_driveCombo->addItem(QDir::toNativeSeparators(path), path);

    const QString current = QDir::fromNativeSeparators(m_currentRemotePath);
    int selectedIndex = -1;
    for (int i = 0; i < m_mountedPaths.size(); ++i)
    {
        const QString mount = QDir::fromNativeSeparators(m_mountedPaths.at(i));
        if (current.startsWith(mount, Qt::CaseInsensitive))
        {
            selectedIndex = i;
            break;
        }
    }

    if (selectedIndex >= 0)
        m_driveCombo->setCurrentIndex(selectedIndex);
    m_updatingDriveCombo = false;
}
