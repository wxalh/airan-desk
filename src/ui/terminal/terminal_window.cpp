#include "terminal_window.h"
#include "common/constant.h"
#include "terminal/emulator/native_terminal_widget.h"
#include "terminal_log_writer.h"
#include "ui_terminal_window.h"
#include "util/json/json_util.h"

#include <QUuid>
#include <QCloseEvent>
#include <QMetaObject>
#include <QTimer>


TerminalWindow::TerminalWindow(QString remoteId, QString remotePwdMd5, WsCli *wsCli, QWidget *parent)
    : QWidget(parent), m_remoteId(remoteId), m_remotePwdMd5(remotePwdMd5),
      m_instanceId(QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'))),
      m_rtcCtl(remoteId, remotePwdMd5, true), m_ws(wsCli)
{
    m_rtcCtl.setSessionLabel(QStringLiteral("terminal-%1").arg(m_instanceId));
    initUI();
    initCLI();
    emit initRtcCtl();
}


TerminalWindow::~TerminalWindow()
{
    m_closing.store(true);
    Q_ASSERT(!m_terminalLogThread.isRunning());
    Q_ASSERT(!m_rtcThread.isRunning());
    m_terminalLogWriter = nullptr;
    delete ui;
}

void TerminalWindow::beginAsyncShutdown()
{
    if (m_closing.exchange(true))
        return;

    hide();
    if (m_started)
    {
        QJsonObject stopMsg = JsonUtil::createObject()
                                  .add(Constant::KEY_MSGTYPE, Constant::TYPE_TERMINAL_STOP)
                                  .build();
        emit fileTextChannelSendMsg(rtc::message_variant(JsonUtil::toCompactBytes(stopMsg).toStdString()));
    }
    if (m_terminal && m_terminalLogWriter)
        disconnect(m_terminal, nullptr, m_terminalLogWriter, nullptr);
    closeTerminalLogFile(true);

    if (m_ws)
    {
        disconnect(m_ws, nullptr, &m_rtcCtl, nullptr);
    }
    disconnect(this, nullptr, &m_rtcCtl, nullptr);
    disconnect(&m_rtcCtl, nullptr, this, nullptr);
    disconnect(&m_rtcCtl, nullptr, m_terminal, nullptr);
    m_rtcCtl.requestShutdown();

    if (m_rtcThread.isRunning())
    {
        QMetaObject::invokeMethod(&m_rtcCtl,
                                  "shutdownAndMoveToOwnerThread",
                                  Qt::QueuedConnection,
                                  Q_ARG(QObject *, this));
    }
    finalizeCloseWhenStopped();
}

void TerminalWindow::finalizeCloseWhenStopped()
{
    if (!m_closing.load() ||
        m_terminalLogThread.isRunning() ||
        m_rtcThread.isRunning())
        return;
    m_shutdownComplete = true;
    QTimer::singleShot(0, this, [this]() { close(); });
}

void TerminalWindow::closeEvent(QCloseEvent *event)
{
    if (m_shutdownComplete)
    {
        event->accept();
        return;
    }
    event->ignore();
    beginAsyncShutdown();
}

bool TerminalWindow::isClosing() const
{
    return m_closing.load();
}
