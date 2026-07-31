#include "terminal/emulator/native_terminal_widget.h"

#include "util/config/config_util.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QMenu>


bool NativeTerminalWidget::clipboardHasText() const
{
    const QClipboard *clipboard = QApplication::clipboard();
    return clipboard && !clipboard->text().isEmpty();
}


void NativeTerminalWidget::showContextMenu(const QPoint &globalPos)
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("Copy"));
    QAction *pasteAction = menu.addAction(tr("Paste"));
    menu.addSeparator();
    QAction *clearAction = menu.addAction(tr("Clear Screen"));
    copyAction->setEnabled(hasSelection());
    pasteAction->setEnabled(clipboardHasText());
    connect(copyAction, &QAction::triggered, this, &NativeTerminalWidget::copySelectionToClipboard);
    connect(pasteAction, &QAction::triggered, this, &NativeTerminalWidget::sendClipboardPaste);
    connect(clearAction, &QAction::triggered, this, [this]() { clearScreenAndScrollback(true); });
    menu.exec(globalPos);
}


void NativeTerminalWidget::copySelectionToClipboard()
{
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QApplication::clipboard()->setText(text);
}
