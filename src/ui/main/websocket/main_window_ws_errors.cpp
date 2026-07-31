#include "ui/main/main_window.h"

#include "common/constant.h"
#include "ui/common/message_box_util.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QLineEdit>
#include <QUuid>


void MainWindow::handleDeviceIdConflict(const QJsonObject &object)
{
    QJsonValue dataVal = object.value(Constant::KEY_DATA);
    if (!dataVal.isObject())
    {
        LOG_ERROR("Invalid data object in DEVICE_ID_CONFLICT message");
        return;
    }

    const QJsonObject data = dataVal.toObject();
    const QString newSessionId = JsonUtil::getString(data, Constant::KEY_NEW_SESSION_ID);
    if (newSessionId.isEmpty() || QUuid(newSessionId).isNull())
    {
        LOG_ERROR("Missing or invalid newSessionId in DEVICE_ID_CONFLICT message");
        return;
    }

    LOG_WARN("Device id conflict detected, replacing local id {} -> {}", ConfigUtil->local_id, newSessionId);
    if (!ConfigUtil->replaceLocalId(newSessionId))
    {
        LOG_ERROR("Device id conflict replacement was not persisted; signaling is being stopped");
        emit resetWsCliUrl(QString());
        UiMessageBox::critical(
            this,
            tr("Identity storage error"),
            tr("The server assigned a new device ID, but it could not be saved. Signaling has been stopped to keep id.ini consistent."));
        return;
    }
    if (m_localIdEdit)
        m_localIdEdit->setText(ConfigUtil->local_id);
    emit resetWsCliUrl(buildWsUrl());
}


QString MainWindow::localizedErrorMessage(const QString &message) const
{
    if (message == Constant::ERROR_PASSWORD_INCORRECT ||
        message == QStringLiteral("password_incorrect") ||
        message == QStringLiteral("remote password incorrect") ||
        message == QStringLiteral("Remote password is incorrect"))
    {
        return tr("Remote verification code is incorrect");
    }

    if (message == QStringLiteral("The controlled end may not be online") ||
        message == QStringLiteral("controlled_offline"))
    {
        return tr("Remote device may be offline");
    }

    if (message == QStringLiteral("not found recv id"))
        return tr("Target device not found");

    if (message == QStringLiteral("Invalid message format"))
        return tr("Invalid signaling format");

    return message;
}
