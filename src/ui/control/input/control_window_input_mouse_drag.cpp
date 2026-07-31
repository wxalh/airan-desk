#include "ui/control/control_window.h"

#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QMouseEvent>
#include <QWidget>


void ControlWindow::mousePressEvent(QMouseEvent *event)
{
    if (m_floatingToolbar)
    {
        const QRect toolbarGlobalRect(m_floatingToolbar->mapToGlobal(QPoint(0, 0)), m_floatingToolbar->size());
        if (toolbarGlobalRect.contains(event->globalPos()))
        {
            m_draggingToolbar = true;
            m_dragStartPosition = event->globalPos();
            m_toolbarOffset = event->globalPos() - m_floatingToolbar->mapToGlobal(QPoint(0, 0));
            event->accept();
            return;
        }
    }

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
                          .add(Constant::KEY_DWFLAGS, Constant::KEY_DOWN)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}


void ControlWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggingToolbar && m_floatingToolbar)
    {
        QWidget *toolbarParent = m_floatingToolbar->parentWidget();
        if (!toolbarParent)
            toolbarParent = this;

        QPoint newPos = toolbarParent->mapFromGlobal(event->globalPos() - m_toolbarOffset);

        int maxX = toolbarParent->width() - m_floatingToolbar->width();
        int maxY = toolbarParent->height() - m_floatingToolbar->height();

        newPos.setX(qMax(0, qMin(newPos.x(), maxX)));
        newPos.setY(qMax(0, qMin(newPos.y(), maxY)));
        m_floatingToolbar->move(newPos);
        m_toolbarUserMoved = true;
        event->accept();
        return;
    }

    sendRemoteMouseMoveAt(event->pos());
}


void ControlWindow::sendRemoteMouseMoveAt(const QPoint &windowPos)
{
    if (!isReceivedImg)
        return;

    QPointF pos = getNormPoint(windowPos);
    if (!isValidNormPoint(pos))
        return;
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_MOUSE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_X, pos.x())
                          .add(Constant::KEY_Y, pos.y())
                          .add(Constant::KEY_DWFLAGS, Constant::KEY_MOVE)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}


void ControlWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_draggingToolbar)
    {
        m_draggingToolbar = false;
        m_toolbarUserMoved = true;
        event->accept();
        return;
    }

    if (!isReceivedImg)
        return;

    QPointF pos = getNormPoint(event->pos());
    if (!isValidNormPoint(pos))
    {
        return;
    }
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_MOUSE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_X, pos.x())
                          .add(Constant::KEY_Y, pos.y())
                          .add(Constant::KEY_BUTTON, static_cast<int>(event->button()))
                          .add(Constant::KEY_DWFLAGS, Constant::KEY_UP)
                          .build();

    QByteArray msg = JsonUtil::toCompactBytes(obj);
    rtc::message_variant msgStr(msg.toStdString());
    emit sendMsg2InputChannel(msgStr);
}
