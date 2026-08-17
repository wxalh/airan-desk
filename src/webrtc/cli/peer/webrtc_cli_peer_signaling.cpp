#include "webrtc/cli/webrtc_cli.h"

#include "webrtc/codec/video_codec_capability_signaling.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "util/qt/qt_callback_util.h"

#include <QPointer>
#include <QThread>

#include <utility>

namespace
{


QString firstVideoCodecFromSdp(const QString &sdp)
{
    const QStringList lines = sdp.split(QLatin1Char('\n'));
    QStringList videoPayloadTypes;
    bool inVideo = false;

    for (const QString &rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.startsWith(QStringLiteral("m=")))
        {
            inVideo = line.startsWith(QStringLiteral("m=video "));
            if (inVideo)
            {
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
                const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
#else
                const QStringList parts = line.split(QLatin1Char(' '), QString::SkipEmptyParts);
#endif
                for (int i = 3; i < parts.size(); ++i)
                    videoPayloadTypes.append(parts.at(i));
            }
            continue;
        }

        if (!inVideo || !line.startsWith(QStringLiteral("a=rtpmap:")))
            continue;

        const int colon = line.indexOf(QLatin1Char(':'));
        const int space = line.indexOf(QLatin1Char(' '), colon + 1);
        if (colon < 0 || space < 0)
            continue;

        const QString payloadType = line.mid(colon + 1, space - colon - 1);
        if (!videoPayloadTypes.contains(payloadType))
            continue;

        const QString codecSpec = line.mid(space + 1);
        return codecSpec.section(QLatin1Char('/'), 0, 0).toUpper();
    }

    return QString();
}

} // namespace


void WebRtcCli::onPeerLocalDescription(rtc::Description description)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, description = std::move(description)]() mutable {
            if (guard)
                guard->onPeerLocalDescription(std::move(description));
        });
        return;
    }
    try
    {
        const QString sdp = QString::fromStdString(std::string(description));
        const QString type = QString::fromStdString(description.typeString());
        const QString codec = firstVideoCodecFromSdp(sdp);
        if (type == QStringLiteral("answer") && !codec.isEmpty() && codec != m_negotiatedVideoCodec)
        {
            m_negotiatedVideoCodec = codec;
            QMetaObject::invokeMethod(this, "notifyCurrentStreamConfig", Qt::QueuedConnection);
            LOG_INFO("Local video codec selected by SDP: {}", m_negotiatedVideoCodec);
        }
        else if (type == QStringLiteral("offer") && !codec.isEmpty())
        {
            LOG_DEBUG("Local SDP first video codec candidate: {}", codec);
        }

        QJsonObject descriptionMsg = JsonUtil::createObject()
                                         .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                         .add(Constant::KEY_TYPE, type)
                                         .add(Constant::KEY_RECEIVER, m_remoteId)
                                         .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                         .add(Constant::KEY_SESSION_ID, m_sessionId)
                                         .add(Constant::KEY_DATA, sdp)
                                         .add(Constant::KEY_VIDEO_CODEC_CAPABILITIES, buildLocalVideoCodecCapabilitiesJson())
                                         .build();

        const QString message = JsonUtil::toCompactString(descriptionMsg);
        emit sendWsCliTextMsg(message);
        LOG_DEBUG("Sent local description type={} to ctl, size={} bytes", type, message.toUtf8().size());
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to send local description: {}", e.what());
    }
    catch (...)
    {
        LOG_ERROR("Unknown error during local description handling");
    }
}


void WebRtcCli::onPeerLocalCandidate(const rtc::Candidate &candidate)
{
    if (m_shutdownStarted.load())
        return;
    if (QThread::currentThread() != thread())
    {
        const rtc::Candidate candidateCopy = candidate;
        const QPointer<WebRtcCli> guard(this);
        m_callbackDispatcher->post([guard, candidateCopy]() {
            if (guard)
                guard->onPeerLocalCandidate(candidateCopy);
        });
        return;
    }
    const QString candidateStr = QString::fromStdString(std::string(candidate));
    const QString midStr = QString::fromStdString(candidate.mid());

    QJsonObject candidateMsg = JsonUtil::createObject()
                                   .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                   .add(Constant::KEY_TYPE, Constant::TYPE_CANDIDATE)
                                   .add(Constant::KEY_RECEIVER, m_remoteId)
                                   .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                   .add(Constant::KEY_SESSION_ID, m_sessionId)
                                   .add(Constant::KEY_DATA, candidateStr)
                                   .add(Constant::KEY_MID, midStr)
                                   .build();

    const QString message = JsonUtil::toCompactString(candidateMsg);
    emit sendWsCliTextMsg(message);
    LOG_DEBUG("Sent local ICE candidate to ctl, mid={}, size={} bytes", midStr, message.toUtf8().size());
}

void WebRtcCli::sendSignalingError(const QString &message)
{
    if (m_shutdownRequested.load() || m_shutdownStarted.load())
        return;

    const QString reason = message.trimmed().isEmpty() ? tr("Remote initialization failed: unknown reason") : message.trimmed();
    QJsonObject errorMsg = JsonUtil::createObject()
                               .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                               .add(Constant::KEY_TYPE, Constant::TYPE_ERROR)
                               .add(Constant::KEY_RECEIVER, m_remoteId)
                               .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                               .add(Constant::KEY_SESSION_ID, m_sessionId)
                               .add(Constant::KEY_DATA, reason)
                               .add(Constant::KEY_ERROR, reason)
                               .build();

    emit sendWsCliTextMsg(JsonUtil::toCompactString(errorMsg));
    LOG_ERROR("Sent signaling error to ctl: {}", reason);
}
