#include "terminal_window.h"

#include "terminal_log_util.h"
#include "terminal_log_writer.h"
#include "terminal/emulator/native_terminal_widget.h"
#include "ui/common/message_box_util.h"
#include "util/config/config_util.h"

#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QMetaObject>

void TerminalWindow::onAutoSaveLogToggled(bool checked)
{
    if (isClosing())
        return;
    setTerminalAutoSave(checked);
}

void TerminalWindow::setTerminalAutoSave(bool enabled)
{
    if (enabled)
    {
        if (!openTerminalLogFile())
        {
            if (m_autoSaveLogCheck)
            {
                m_autoSaveLogCheck->blockSignals(true);
                m_autoSaveLogCheck->setChecked(false);
                m_autoSaveLogCheck->blockSignals(false);
            }
            m_terminalLogEnabled = false;
            updateTerminalLogUi();
            if (RuntimeEnvironment::uiAvailable())
                UiMessageBox::warning(this, tr("Terminal Log"), tr("Unable to create terminal log file."));
            return;
        }
        m_terminalLogEnabled = false;
    }
    else
    {
        m_terminalLogEnabled = false;
        emit terminalConsumerBacklogChanged(0);
        closeTerminalLogFile();
    }
    updateTerminalLogUi();
}

bool TerminalWindow::openTerminalLogFile()
{
    if (!m_terminalLogWriter)
    {
        m_terminalLogWriter = new TerminalLogWriter();
        m_terminalLogWriter->moveToThread(&m_terminalLogThread);
        connect(&m_terminalLogThread, &QThread::finished, m_terminalLogWriter, &QObject::deleteLater);
        connect(&m_terminalLogThread, &QThread::finished, this, &TerminalWindow::finalizeCloseWhenStopped);
        connect(m_terminalLogWriter, &TerminalLogWriter::errorOccurred,
                this, &TerminalWindow::onTerminalLogError);
        connect(m_terminalLogWriter, &TerminalLogWriter::openFinished,
                this, &TerminalWindow::onTerminalLogOpenFinished);
        connect(m_terminalLogWriter, &TerminalLogWriter::closeFinished,
                &m_terminalLogThread, &QThread::quit, Qt::DirectConnection);
        connect(m_terminalLogWriter, &TerminalLogWriter::pendingBytesChanged,
                this, [this](qint64 bytes) { emit terminalConsumerBacklogChanged(bytes); });
        connect(m_terminal, &NativeTerminalWidget::processedOutput,
                m_terminalLogWriter,
                [writer = m_terminalLogWriter](const QByteArray &data) { writer->enqueue(data); },
                Qt::DirectConnection);
        m_terminalLogThread.setObjectName(QStringLiteral("TerminalWindow-LogWriterThread"));
        m_terminalLogThread.start();
    }

    m_terminalLogPath = TerminalLogUtil::defaultTerminalLogPath(m_remoteId, m_instanceId);
    return QMetaObject::invokeMethod(m_terminalLogWriter, "openLogAsync",
                                     Qt::QueuedConnection,
                                     Q_ARG(QString, m_terminalLogPath),
                                     Q_ARG(QString, m_remoteId));
}

void TerminalWindow::closeTerminalLogFile(bool finishThread)
{
    if (!m_terminalLogWriter || !m_terminalLogThread.isRunning())
        return;
    QMetaObject::invokeMethod(m_terminalLogWriter,
                              finishThread ? "closeLogAndFinish" : "closeLog",
                              Qt::QueuedConnection);
}

void TerminalWindow::appendTerminalLogMarker(const QString &marker)
{
    if (!m_terminalLogEnabled || !m_terminalLogWriter || marker.isEmpty())
        return;
    QMetaObject::invokeMethod(m_terminalLogWriter, "appendMarker", Qt::QueuedConnection,
                              Q_ARG(QString, marker));
}

void TerminalWindow::onTerminalLogError(const QString &message)
{
    if (isClosing())
        return;
    if (!m_terminalLogEnabled)
        return;
    m_terminalLogEnabled = false;
    emit terminalConsumerBacklogChanged(0);
    if (m_autoSaveLogCheck)
    {
        m_autoSaveLogCheck->blockSignals(true);
        m_autoSaveLogCheck->setChecked(false);
        m_autoSaveLogCheck->blockSignals(false);
    }
    updateTerminalLogUi();
    if (RuntimeEnvironment::uiAvailable())
        UiMessageBox::warning(this, tr("Terminal Log"),
                              tr("Unable to write terminal log file: %1").arg(message));
}

void TerminalWindow::onTerminalLogOpenFinished(bool ok, const QString &errorMessage)
{
    if (isClosing())
        return;
    if (ok)
    {
        m_terminalLogEnabled = true;
        updateTerminalLogUi();
        return;
    }

    m_terminalLogEnabled = false;
    if (m_autoSaveLogCheck)
    {
        m_autoSaveLogCheck->blockSignals(true);
        m_autoSaveLogCheck->setChecked(false);
        m_autoSaveLogCheck->blockSignals(false);
    }
    updateTerminalLogUi();
    if (RuntimeEnvironment::uiAvailable())
        UiMessageBox::warning(this, tr("Terminal Log"),
                              errorMessage.isEmpty()
                                  ? tr("Unable to create terminal log file.")
                                  : errorMessage);
}

void TerminalWindow::updateTerminalLogUi()
{
    if (!m_autoSaveLogPathLabel)
        return;

    if (m_terminalLogEnabled && !m_terminalLogPath.isEmpty())
        m_autoSaveLogPathLabel->setText(tr("Recording to: %1").arg(QDir::toNativeSeparators(m_terminalLogPath)));
    else
        m_autoSaveLogPathLabel->setText(tr("Disabled"));
}
