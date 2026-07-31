#include "webrtc/ctl/webrtc_ctl.h"
#include "webrtc/codec/video_codec_capability_signaling.h"
#include "common/constant.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThread>

#include <utility>

namespace
{
/*
 * Converts an SDP description type into user-visible text.
 */
QString descriptionTypeText(const QString &type, QObject *context)
{
    Q_UNUSED(context);
    if (type == Constant::TYPE_OFFER)
        return QCoreApplication::translate("WebRtcCtl", "offer");
    if (type == Constant::TYPE_ANSWER)
        return QCoreApplication::translate("WebRtcCtl", "answer");
    return type;
}
} // namespace

/*
 * Sends a local SDP description to the remote side.
 */
void WebRtcCtl::onPeerLocalDescription(rtc::Description description)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, description = std::move(description)]() mutable {
            if (guard)
                guard->onPeerLocalDescription(std::move(description));
        });
        return;
    }
    try
    {
        QString sdp = QString::fromStdString(std::string(description));
        QString type = QString::fromStdString(description.typeString());

        QJsonObject descriptionMsg = JsonUtil::createObject()
                                         .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                         .add(Constant::KEY_TYPE, type)
                                         .add(Constant::KEY_RECEIVER, m_remoteId)
                                         .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                         .add(Constant::KEY_SESSION_ID, m_sessionId)
                                         .add(Constant::KEY_DATA, sdp)
                                         .add(Constant::KEY_VIDEO_CODEC_CAPABILITIES, buildLocalVideoCodecCapabilitiesJson())
                                         .build();

        QString message = JsonUtil::toCompactString(descriptionMsg);
        emit sendWsCliTextMsg(message);
        emit connectionStatusChanged(tr("Sent local %1, waiting for media connection...")
                                         .arg(descriptionTypeText(type, this)));
        LOG_DEBUG("Sent local description type={} to cli, size={} bytes", type, message.toUtf8().size());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send local description: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Failed to send local description: unknown error");
    }
}

/*
 * Sends a local ICE candidate to the remote side.
 */
void WebRtcCtl::onPeerLocalCandidate(const rtc::Candidate &candidate)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const rtc::Candidate candidateCopy = candidate;
        const QPointer<WebRtcCtl> guard(this);
        m_callbackDispatcher->post([guard, candidateCopy]() {
            if (guard)
                guard->onPeerLocalCandidate(candidateCopy);
        });
        return;
    }
    QString candidateStr = QString::fromStdString(std::string(candidate));
    QString midStr = QString::fromStdString(candidate.mid());

    QJsonObject candidateMsg = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CTL)
                                   .add(Constant::KEY_TYPE, Constant::TYPE_CANDIDATE)
                                   .add(Constant::KEY_RECEIVER, m_remoteId)
                                   .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                   .add(Constant::KEY_SESSION_ID, m_sessionId)
                                   .add(Constant::KEY_DATA, candidateStr)
                                   .add(Constant::KEY_MID, midStr)
                                   .build();

    QString message = JsonUtil::toCompactString(candidateMsg);
    emit sendWsCliTextMsg(message);
    noteLocalNetworkCandidate(candidateStr);
    LOG_DEBUG("Sent local ICE candidate to cli, mid={}, size={} bytes", midStr, message.toUtf8().size());
}
