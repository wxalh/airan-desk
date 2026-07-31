#include "terminal/file_panel/terminal_file_panel.h"

#include "common/constant.h"
#include "util/text/convert_util.h"
#include "util/json/json_util.h"

#include <QTableWidget>
#include <QTableWidgetItem>


void TerminalFilePanel::populateRemoteFiles()
{
    m_remoteTable->setRowCount(m_remoteFiles.size() + 1);
    auto *parent = new QTableWidgetItem(QStringLiteral(".."));
    parent->setIcon(m_dirIcon);
    parent->setData(Qt::UserRole, true);
    m_remoteTable->setItem(0, 0, parent);
    m_remoteTable->setItem(0, 1, new QTableWidgetItem());
    m_remoteTable->setItem(0, 2, new QTableWidgetItem());

    for (int i = 0; i < m_remoteFiles.size(); ++i)
    {
        const QJsonObject obj = m_remoteFiles.at(i).toObject();
        const int row = i + 1;
        const bool isDir = JsonUtil::getBool(obj, Constant::KEY_IS_DIR);
        auto *nameItem = new QTableWidgetItem(JsonUtil::getString(obj, Constant::KEY_NAME));
        nameItem->setIcon(isDir ? m_dirIcon : m_fileIcon);
        nameItem->setData(Qt::UserRole, isDir);
        m_remoteTable->setItem(row, 0, nameItem);

        const qint64 size = JsonUtil::getInt64(obj, Constant::KEY_FILE_SIZE);
        m_remoteTable->setItem(row, 1, new QTableWidgetItem(isDir || size <= 0 ? QString() : ConvertUtil::formatFileSize(size)));
        m_remoteTable->setItem(row, 2, new QTableWidgetItem(JsonUtil::getString(obj, Constant::KEY_FILE_LAST_MOD_TIME)));
    }
}
