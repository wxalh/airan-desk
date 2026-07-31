#include "ui/control/control_window.h"

#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QMouseEvent>
#include <QWheelEvent>


void ControlWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!isReceivedImg)
        return;

    syncLocalClipboardToRemoteIfNeeded();

    QPointF pos = getNormPoint(event->pos());
    if (!isValidNormPoint(pos))
        return;
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_MOUSE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_X, pos.x())
                          .add(Constant::KEY_Y, pos.y())
                          .add(Constant::KEY_BUTTON, static_cast<int>(event->button()))
                          .add(Constant::KEY_DWFLAGS, Constant::KEY_DOUBLECLICK)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}


void ControlWindow::wheelEvent(QWheelEvent *event)
{
    if (!isReceivedImg)
        return;

    syncLocalClipboardToRemoteIfNeeded();

#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    QPointF pos = getNormPoint(event->position().toPoint());
#else
    QPointF pos = getNormPoint(event->pos());
#endif
    if (!isValidNormPoint(pos))
        return;
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_MOUSE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_X, pos.x())
                          .add(Constant::KEY_Y, pos.y())
                          .add(Constant::KEY_DWFLAGS, Constant::KEY_WHEEL)
                          .add(Constant::KEY_MOUSEDATA, event->angleDelta().y())
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}
