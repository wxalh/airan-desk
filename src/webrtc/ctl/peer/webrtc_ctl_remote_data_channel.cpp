#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/qt/qt_callback_util.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <utility>

namespace
{
/*
 * Converts a remote data channel label into user-visible text.
 */
QString channelLabelText(const QString &label, QObject *context)
{
    Q_UNUSED(context);
    if (label == Constant::TYPE_INPUT || label == Constant::TYPE_INPUT_MOVE)
        return QCoreApplication::translate("WebRtcCtl", "input");
    if (label == Constant::TYPE_FILE)
        return QCoreApplication::translate("WebRtcCtl", "file");
    if (label == Constant::TYPE_FILE_TEXT)
        return QCoreApplication::translate("WebRtcCtl", "file text");
    if (label == Constant::TYPE_CLIPBOARD)
        return QCoreApplication::translate("WebRtcCtl", "clipboard");
    if (label == Constant::TYPE_SESSION_HEARTBEAT)
        return QCoreApplication::translate("WebRtcCtl", "heartbeat");
    return label;
}
} // namespace

/*
 * Handles remote data channel creation and binds callbacks by label.
 */
void WebRtcCtl::onRemoteDataChannel(std::shared_ptr<rtc::DataChannel> channel)
{
    if (!channel || m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, channel = std::move(channel)]() mutable {
            if (guard)
                guard->onRemoteDataChannel(std::move(channel));
        });
        return;
    }
    QString channelLabel = QString::fromStdString(channel->label());
    LOG_INFO("Control side received data channel: {}", channelLabel);
    emit connectionStatusChanged(tr("Data channel created: %1").arg(channelLabelText(channelLabel, this)));

    if (channelLabel == Constant::TYPE_FILE)
    {
        m_fileChannel = channel;
        setupFileChannelCallbacks();
    }
    else if (channelLabel == Constant::TYPE_FILE_TEXT)
    {
        m_fileTextChannel = channel;
        setupFileTextChannelCallbacks();
    }
    else if (channelLabel == Constant::TYPE_INPUT)
    {
        m_inputChannel = channel;
        setupInputChannelCallbacks();
    }
    else if (channelLabel == Constant::TYPE_INPUT_MOVE)
    {
        m_inputMoveChannel = channel;
        setupInputMoveChannelCallbacks();
    }
    else if (channelLabel == Constant::TYPE_CLIPBOARD)
    {
        m_clipboardChannel = channel;
        setupClipboardChannelCallbacks();
    }
    else if (channelLabel == Constant::TYPE_SESSION_HEARTBEAT)
    {
        m_heartbeatChannel = channel;
        setupHeartbeatChannelCallbacks();
    }
}
