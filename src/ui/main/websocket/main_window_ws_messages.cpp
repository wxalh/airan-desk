#include "ui/main/main_window.h"

#include "ui/common/message_box_util.h"
#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"


void MainWindow::onWsCliRecvTextMsg(const QString &message)
{
    onWsCliRecvBinaryMsg(message.toUtf8());
}


void MainWindow::onWsCliRecvBinaryMsg(const QByteArray &message)
{
    QJsonObject object = JsonUtil::safeParseObject(message);
    if (!JsonUtil::isValidObject(object))
    {
        LOG_ERROR("Failed to parse JSON in main window");
        return;
    }

    QString sender = JsonUtil::getString(object, Constant::KEY_SENDER);
    QString type = JsonUtil::getString(object, Constant::KEY_TYPE);

    if (sender.isEmpty() || type.isEmpty())
    {
        LOG_ERROR("Missing sender or type in message");
        return;
    }
    if (sender == Constant::ROLE_SERVER)
    {
        if (type == Constant::TYPE_DEVICE_ID_CONFLICT)
        {
            handleDeviceIdConflict(object);
        }
        else if (type == Constant::TYPE_ERROR)
        {
            QString data = JsonUtil::getString(object, Constant::KEY_DATA);
            if (data.isEmpty())
            {
                LOG_ERROR("Invalid message: missing data");
                return;
            }

            LOG_ERROR("Remote error: {}", data);
            const QString role = JsonUtil::getString(object, Constant::KEY_ROLE);
            if ((data == QStringLiteral("The controlled end may not be online") ||
                 data == QStringLiteral("controlled_offline")) &&
                role != Constant::ROLE_CTL)
            {
                LOG_WARN("Suppressing remote-offline popup on non-controller role: role={}", role);
                return;
            }
            if (RuntimeEnvironment::uiAvailable())
                UiMessageBox::critical(nullptr, tr("Error"), localizedErrorMessage(data));
        }
    }
    else if (type == Constant::TYPE_CONNECT)
    {
        handleIncomingConnectRequest(sender, object);
    }
    else if (type == Constant::TYPE_PEER_DISCONNECT)
    {
        const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
        const QString role = JsonUtil::getString(object, Constant::KEY_ROLE);
        if (receiver == ConfigUtil->local_id && role == Constant::ROLE_CLI && m_trayIcon)
        {
            m_trayIcon->showMessage(
                tr("Remote connections closed"),
                tr("Remote device %1 closed all connections.").arg(sender),
                QSystemTrayIcon::Information,
                5000);
        }
    }
    else if (type == Constant::TYPE_ERROR)
    {
        const QString receiver = JsonUtil::getString(object, Constant::KEY_RECEIVER);
        if (!receiver.isEmpty() && receiver != ConfigUtil->local_id)
        {
            LOG_TRACE("Ignore peer error for unrelated receiver: {}", receiver);
            return;
        }

        QString data = JsonUtil::getString(object, Constant::KEY_DATA);
        if (data.isEmpty())
        {
            LOG_ERROR("Received error message without data");
            return;
        }

        LOG_ERROR("Peer error: {}", data);
        if (RuntimeEnvironment::uiAvailable())
            UiMessageBox::critical(nullptr, tr("Error"), localizedErrorMessage(data));
    }
}
