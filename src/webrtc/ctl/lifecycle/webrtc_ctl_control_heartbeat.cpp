#include "webrtc/ctl/webrtc_ctl.h"

#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QTimer>


void WebRtcCtl::sendControlHeartbeat()
{
    if (m_isOnlyFile)
        return;

    if (!m_inputChannel || !m_inputChannel->isOpen())
    {
        if (m_controlHeartbeatTimer)
            m_controlHeartbeatTimer->stop();
        return;
    }

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_CONTROL_HEARTBEAT)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, m_remoteId)
                          .add(Constant::KEY_RECEIVER_PWD, m_remotePwdMd5)
                          .build();
    try
    {
        m_inputChannel->send(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
        noteSessionOutboundActivity();
    }
    catch (const std::exception &e)
    {
        LOG_WARN("Failed to send control heartbeat: {}", e.what());
    }
}
