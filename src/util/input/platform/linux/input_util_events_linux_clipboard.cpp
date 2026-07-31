#include "util/input/platform/linux/input_util_events_linux_internal.h"

#if defined(Q_OS_LINUX)
#include <QClipboard>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>

namespace input_linux_internal
{

namespace
{

bool readQtClipboardText(QByteArray *out)
{
    QClipboard *clipboard = qApp ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard)
        return false;
    if (out)
        *out = clipboard->text(QClipboard::Clipboard).toUtf8();
    return true;
}

bool setQtClipboardText(const QByteArray &bytes)
{
    QClipboard *clipboard = qApp ? QGuiApplication::clipboard() : nullptr;
    if (!clipboard)
        return false;
    clipboard->setText(QString::fromUtf8(bytes), QClipboard::Clipboard);
    return true;
}

} // namespace

bool runClipboardWriter(const QString &program, const QStringList &args, const QByteArray &bytes)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(1000))
        return false;
    process.write(bytes);
    process.closeWriteChannel();
    if (!process.waitForFinished(2000))
    {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

bool runClipboardReader(const QString &program, const QStringList &args, QByteArray *out)
{
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(1000))
        return false;
    if (!process.waitForFinished(2000))
    {
        process.kill();
        process.waitForFinished(500);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return false;
    if (out)
        *out = process.readAllStandardOutput();
    return true;
}
ClipboardTool findClipboardTool()
{
    const bool wayland = !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");
    if (wayland)
    {
        const QString wlCopy = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
        const QString wlPaste = QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
        if (!wlCopy.isEmpty() && !wlPaste.isEmpty())
            return ClipboardTool{wlCopy, QStringList{QStringLiteral("--no-newline")}, QStringList()};
    }
     const QString xclip = QStandardPaths::findExecutable(QStringLiteral("xclip"));
    if (!xclip.isEmpty())
        return ClipboardTool{xclip,
                             QStringList{QStringLiteral("-selection"), QStringLiteral("clipboard"), QStringLiteral("-o")},
                             QStringList{QStringLiteral("-selection"), QStringLiteral("clipboard")}};
     const QString xsel = QStandardPaths::findExecutable(QStringLiteral("xsel"));
    if (!xsel.isEmpty())
        return ClipboardTool{xsel,
                             QStringList{QStringLiteral("--clipboard"), QStringLiteral("--output")},
                             QStringList{QStringLiteral("--clipboard"), QStringLiteral("--input")}};
     if (!wayland)
    {
        const QString wlCopy = QStandardPaths::findExecutable(QStringLiteral("wl-copy"));
        const QString wlPaste = QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
        if (!wlCopy.isEmpty() && !wlPaste.isEmpty())
            return ClipboardTool{wlCopy, QStringList{QStringLiteral("--no-newline")}, QStringList()};
    }
     return ClipboardTool();
}

bool setClipboardText(const ClipboardTool &tool, const QByteArray &bytes)
{
    if (tool.program.isEmpty())
        return setQtClipboardText(bytes);
    return runClipboardWriter(tool.program, tool.writeArgs, bytes);
}

bool readClipboardText(const ClipboardTool &tool, QByteArray *out)
{
    if (tool.program.isEmpty())
        return readQtClipboardText(out);
     if (tool.program.endsWith(QStringLiteral("wl-copy")))
    {
        const QString wlPaste = QStandardPaths::findExecutable(QStringLiteral("wl-paste"));
        return !wlPaste.isEmpty() && runClipboardReader(wlPaste, tool.readArgs, out);
    }
     return runClipboardReader(tool.program, tool.readArgs, out);
}

} // namespace input_linux_internal
#endif
