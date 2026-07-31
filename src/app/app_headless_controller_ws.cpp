#include "app/app_headless_controller.h"

#include "common/constant.h"
#include "common/logger_manager.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"
#include "websocket/signaling_url_resolver.h"

#include <QHostInfo>
#include <QJsonValue>
#include <QMetaObject>
#include <QUuid>


QString HeadlessController::buildWsUrl() const
{
    return SignalingUrlResolver::resolve(
               ConfigUtil->wsUrl,
               ConfigUtil->local_id,
               QHostInfo::localHostName(),
               ConfigUtil->install_id)
        .url;
}


void HeadlessController::handleDeviceIdConflict(const QJsonObject &object)
{
    const QJsonValue dataVal = object.value(Constant::KEY_DATA);
    if (!dataVal.isObject())
        return;

    const QString newSessionId = JsonUtil::getString(dataVal.toObject(), Constant::KEY_NEW_SESSION_ID);
    if (newSessionId.isEmpty() || QUuid(newSessionId).isNull())
        return;

    LOG_WARN("Device id conflict detected in headless mode, replacing local id {} -> {}",
             ConfigUtil->local_id,
             newSessionId);
    if (!ConfigUtil->replaceLocalId(newSessionId))
    {
        LOG_ERROR("Headless device id conflict replacement was not persisted; signaling is being stopped");
        QMetaObject::invokeMethod(m_ws, "resetUrlAndReconnect", Qt::QueuedConnection,
                                  Q_ARG(QString, QString()));
        return;
    }
    QMetaObject::invokeMethod(m_ws, "resetUrlAndReconnect", Qt::QueuedConnection, Q_ARG(QString, buildWsUrl()));
}


void HeadlessController::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    if (m_shuttingDown)
        return;
    QJsonObject object = JsonUtil::safeParseObject(message);
    if (!JsonUtil::isValidObject(object))
        return;

    const QString sender = JsonUtil::getString(object, Constant::KEY_SENDER);
    const QString type = JsonUtil::getString(object, Constant::KEY_TYPE);
    if (sender.isEmpty() || type.isEmpty())
        return;

    if (sender == Constant::ROLE_SERVER)
    {
        if (type == Constant::TYPE_DEVICE_ID_CONFLICT)
            handleDeviceIdConflict(object);
        else if (type == Constant::TYPE_ERROR)
            LOG_ERROR("Server error in headless mode: {}", JsonUtil::getString(object, Constant::KEY_DATA));
        return;
    }

    if (type == Constant::TYPE_CONNECT)
    {
        handleIncomingConnectRequest(sender, object);
    }
    else if (type == Constant::TYPE_ERROR)
    {
        LOG_ERROR("Peer error in headless mode: {}", JsonUtil::getString(object, Constant::KEY_DATA));
    }
}
