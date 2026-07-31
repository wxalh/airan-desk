

#include "ui/control/control_window.h"

#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QAction>
#include <QActionGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QToolButton>

void ControlWindow::sendSwitchScreenRequest(const QString &screenId)
{
    if (screenId.isEmpty())
    {
        LOG_WARN("Switch screen request ignored: no remote screen selected");
        return;
    }
    if (screenId == m_remoteScreenId)
    {
        LOG_DEBUG("Switch screen request ignored: screen already selected: {}", screenId);
        return;
    }

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_SWITCH_SCREEN)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_SCREEN_ID, screenId)
                          .build();

    rtc::message_variant msgStr(JsonUtil::toCompactBytes(obj).toStdString());
    emit sendMsg2InputChannel(msgStr);
    LOG_INFO("Switch screen request sent to remote: {}, screenId={}", remote_id, screenId);
}

void ControlWindow::rebuildScreenMenu()
{
    if (!m_screenMenu)
        return;

    m_screenMenu->clear();
    const auto oldGroups = m_screenMenu->findChildren<QActionGroup *>(QStringLiteral("remoteScreenActionGroup"),
                                                                      Qt::FindDirectChildrenOnly);
    for (QActionGroup *group : oldGroups)
        delete group;

    if (m_remoteScreens.isEmpty())
    {
        QAction *empty = m_screenMenu->addAction(tr("No remote screens"));
        empty->setEnabled(false);
        if (m_switchScreenBtn)
            m_switchScreenBtn->setToolTip(tr("Select a remote screen"));
        return;
    }

    QActionGroup *screenGroup = new QActionGroup(m_screenMenu);
    screenGroup->setObjectName(QStringLiteral("remoteScreenActionGroup"));
    screenGroup->setExclusive(true);

    for (int i = 0; i < m_remoteScreens.size(); ++i)
    {
        const QJsonObject screen = m_remoteScreens.at(i).toObject();
        QString screenId = screen.value(Constant::KEY_SCREEN_ID).toString();
        if (screenId.isEmpty())
            continue;

        const int remoteIndex = screen.value(Constant::KEY_SCREEN_INDEX).toInt(i);
        const int width = screen.value(Constant::KEY_WIDTH).toInt();
        const int height = screen.value(Constant::KEY_HEIGHT).toInt();
        QString text = width > 0 && height > 0
                           ? tr("Screen %1 - %2x%3").arg(remoteIndex + 1).arg(width).arg(height)
                           : tr("Screen %1").arg(remoteIndex + 1);
        if (screenId == m_remoteScreenId)
            text.append(tr(" (current)"));

        QAction *action = m_screenMenu->addAction(text);
        action->setCheckable(true);
        screenGroup->addAction(action);
        action->setChecked(screenId == m_remoteScreenId);
        action->setData(screenId);
        connect(action, &QAction::triggered, this, [this, action, screenId]()
        {
            if (screenId == m_remoteScreenId)
            {
                action->setChecked(true);
                return;
            }
            sendSwitchScreenRequest(screenId);
        });
    }
    if (!screenGroup->checkedAction() && !screenGroup->actions().isEmpty())
        screenGroup->actions().first()->setChecked(true);

    if (m_switchScreenBtn)
        m_switchScreenBtn->setToolTip(tr("Select a remote screen"));
}

void ControlWindow::onRemoteScreensChanged(const QJsonArray &screens, const QString &currentScreenId)
{
    m_remoteScreens = screens;
    m_remoteScreenId = currentScreenId;
    rebuildScreenMenu();
    LOG_INFO("Remote screen catalog updated: count={}, current={}", m_remoteScreens.size(), m_remoteScreenId);
}


void ControlWindow::sendAndroidNavigation(const QString &action)
{
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_ANDROID_NAVIGATION)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_ACTION, action)
                          .build();

    rtc::message_variant msgStr(JsonUtil::toCompactBytes(obj).toStdString());
    emit sendMsg2InputChannel(msgStr);
    LOG_INFO("Android navigation requested: {}", action);
}


void ControlWindow::sendRemoteKeyTap(int winKey)
{
    sendRemoteShortcut(QList<int>{winKey});
}


void ControlWindow::sendRemoteShortcut(const QList<int> &winKeys)
{
    if (winKeys.isEmpty())
        return;

    auto sendKey = [this](int winKey, const QString &flags)
    {
        QJsonObject obj = JsonUtil::createObject()
                              .add(Constant::KEY_MSGTYPE, Constant::TYPE_KEYBOARD)
                              .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                              .add(Constant::KEY_RECEIVER, remote_id)
                              .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                              .add(Constant::KEY_KEY, winKey)
                              .add(Constant::KEY_DWFLAGS, flags)
                              .build();
        emit sendMsg2InputChannel(rtc::message_variant(JsonUtil::toCompactBytes(obj).toStdString()));
    };

    for (int key : winKeys)
        sendKey(key, Constant::KEY_DOWN);
    for (auto it = winKeys.crbegin(); it != winKeys.crend(); ++it)
        sendKey(*it, Constant::KEY_UP);
    LOG_INFO("Remote keyboard shortcut sent: {}", winKeys.size());
}


void ControlWindow::onRemoteOsChanged(const QString &osName)
{
    m_remoteOsName = osName.trimmed();
    const bool android = osName.trimmed().toLower().contains(QStringLiteral("android"));
    setAndroidNavigationVisible(android);
    LOG_INFO("Remote OS changed: {}, androidNavigationVisible={}", osName, android);
}


void ControlWindow::onRemoteOperationTriggered()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    QAction *selected = qobject_cast<QAction *>(sender());
    if (!selected)
        return;

    const QString action = selected->data().toString();
    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_REMOTE_OPERATION)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_ACTION, action)
                          .build();

    rtc::message_variant msgStr(JsonUtil::toCompactBytes(obj).toStdString());
    emit sendMsg2InputChannel(msgStr);
    LOG_INFO("Remote operation requested: {}", action);
}
