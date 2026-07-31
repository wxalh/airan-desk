#include "terminal/emulator/native_terminal_widget.h"

#include <QApplication>
#include <QClipboard>
#include <QMetaObject>
#include <QMutexLocker>

void NativeTerminalWidget::writePtyOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    QMutexLocker locker(&m_workerMutex);
    if (m_workerClosing.load())
        return;
    TerminalEmulatorWorker *worker = m_worker;
    if (!worker)
        return;
    QMetaObject::invokeMethod(worker, "enqueueOutput", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));
}

void NativeTerminalWidget::showStatusLine(const QString &message)
{
    if (!m_worker || m_workerClosing.load())
        return;
    const QByteArray data = (QStringLiteral("\r\n") + message + QStringLiteral("\r\n")).toUtf8();
    QMetaObject::invokeMethod(m_worker, "enqueueLocalOutput", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));
}

void NativeTerminalWidget::sendKey(VTermKey key, VTermModifier modifiers)
{
    if (!m_worker || m_workerClosing.load())
        return;
    QMetaObject::invokeMethod(m_worker, "sendKey", Qt::QueuedConnection,
                              Q_ARG(int, static_cast<int>(key)),
                              Q_ARG(int, static_cast<int>(modifiers)));
}

void NativeTerminalWidget::sendText(const QString &text, VTermModifier modifiers)
{
    if (!m_worker || m_workerClosing.load() || text.isEmpty())
        return;
    QMetaObject::invokeMethod(m_worker, "sendText", Qt::QueuedConnection,
                              Q_ARG(QString, text),
                              Q_ARG(int, static_cast<int>(modifiers)));
}

void NativeTerminalWidget::sendInputBytes(const QByteArray &data)
{
    if (!m_worker || m_workerClosing.load() || data.isEmpty())
        return;
    QMetaObject::invokeMethod(m_worker, "sendInputBytes", Qt::QueuedConnection,
                              Q_ARG(QByteArray, data));
}

void NativeTerminalWidget::sendClipboardPaste()
{
    const QString text = QApplication::clipboard()->text();
    if (!m_worker || m_workerClosing.load() || text.isEmpty())
        return;
    QMetaObject::invokeMethod(m_worker, "sendClipboardPaste", Qt::QueuedConnection,
                              Q_ARG(QString, text));
}
