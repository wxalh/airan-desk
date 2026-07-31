#include "webrtc/ctl/webrtc_ctl.h"
#include "util/qt/qt_callback_util.h"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include <utility>

namespace
{
/*
 * Converts a remote media track type into user-visible text.
 */
QString trackTypeText(const QString &type, QObject *context)
{
    Q_UNUSED(context);
    if (type == QStringLiteral("video"))
        return QCoreApplication::translate("WebRtcCtl", "video");
    if (type == QStringLiteral("audio"))
        return QCoreApplication::translate("WebRtcCtl", "audio");
    return type;
}
} // namespace

/*
 * Handles remote media track creation and installs video/audio callbacks.
 */
void WebRtcCtl::onRemoteTrack(std::shared_ptr<rtc::Track> track)
{
    if (!track || m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, track = std::move(track)]() mutable {
            if (guard)
                guard->onRemoteTrack(std::move(track));
        });
        return;
    }

    const QString trackMid = QString::fromStdString(track->mid());
    const QString trackType = QString::fromStdString(track->description().type());
    LOG_INFO("Control side received remote track: mid={}, type={}", trackMid, trackType);
    emit connectionStatusChanged(tr("Received remote %1 track, waiting for first frame...")
                                     .arg(trackTypeText(trackType, this)));

    if (trackType == QStringLiteral("video"))
    {
        if (m_videoTrack && m_videoTrack != track)
        {
            m_videoTrack->resetCallbacks();
            m_videoTrack->close();
        }
        m_videoTrack = track;
        QPointer<WebRtcCtl> weakThis(this);
        const auto callbackLifetime = m_callbackLifetime;
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        m_videoTrack->onD3D11Frame([weakThis, callbackLifetime](rtc::D3D11VideoFrame frame, rtc::FrameInfo info) mutable {
            auto permit = callbackLifetime->tryEnter();
            if (!permit)
                return;
            if (!weakThis)
                return;
            frame.timestampUs = static_cast<qint64>(info.timestamp);
            QMetaObject::invokeMethod(weakThis.data(),
                                      [weakThis, frame]() mutable {
                                          if (!weakThis)
                                              return;
                                          weakThis->onD3D11VideoFrameReceived(std::move(frame));
                                      },
                                      Qt::QueuedConnection);
        });
        LOG_INFO("Native D3D11 video rendering enabled on control side");
#endif
        m_videoTrack->onFrame([weakThis, callbackLifetime](rtc::binary data, rtc::FrameInfo info) mutable {
            auto permit = callbackLifetime->tryEnter();
            if (!permit)
                return;
            if (!weakThis)
                return;
            const QByteArray bytes(reinterpret_cast<const char *>(data.data()), static_cast<int>(data.size()));
            QMetaObject::invokeMethod(weakThis.data(), "onVideoFrameBytesReceived", Qt::QueuedConnection,
                                      Q_ARG(QByteArray, bytes),
                                      Q_ARG(qint64, static_cast<qint64>(info.timestamp)));
        });
        LOG_INFO("Remote video track callback installed for mid={}", trackMid);
        return;
    }

    if (trackType == QStringLiteral("audio"))
    {
        if (m_remoteAudioTrack && m_remoteAudioTrack != track)
        {
            m_remoteAudioTrack->resetCallbacks();
            m_remoteAudioTrack->close();
        }
        m_remoteAudioTrack = track;
        m_remoteAudioTrack->setEnabled(m_audioMode == QStringLiteral("listen") || m_audioMode == QStringLiteral("call"));
        LOG_INFO("Remote audio track attached to libwebrtc audio device for mid={}", trackMid);
    }
}
