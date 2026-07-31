#include "terminal/file_panel/terminal_file_panel.h"

#include <QApplication>
#include <QStyle>


TerminalFilePanel::TerminalFilePanel(QWidget *parent)
    : QWidget(parent)
{
    m_dirIcon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
    m_fileIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);
    setupUi();
}
