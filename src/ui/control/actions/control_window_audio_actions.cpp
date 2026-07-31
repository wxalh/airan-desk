#include "ui/control/control_window.h"

#include "ui/control/control_window_view_helpers.h"
#include "util/config/config_util.h"
#include "util/json/json_util.h"

#include <QAction>
#include <QCoreApplication>
#include <QMessageBox>
#include <QMetaObject>
#include <QSignalBlocker>
#include <QToolButton>
#include <QUuid>

namespace
{
/*
 * Converts an audio mode protocol value into user-visible text.
 */
QString audioModeLabelForUi(const QString &mode, QObject *context)
{
    Q_UNUSED(context);
    if (mode == QStringLiteral("listen"))
        return QCoreApplication::translate("ControlWindow", "Listen");
    if (mode == QStringLiteral("call"))
        return QCoreApplication::translate("ControlWindow", "Call");
    return QCoreApplication::translate("ControlWindow", "Audio off");
}
} // namespace

/*
 * Refreshes the toolbar audio button and audio mode menu state.
 */
void ControlWindow::updateAudioButtonState(const QString &mode, bool pending)
{
    if (!m_audioCaptureBtn)
        return;

    m_audioCaptureBtn->setText(tr("Audio"));
    m_audioCaptureBtn->setToolTip(pending
                                      ? tr("Requesting remote audio mode confirmation: %1").arg(audioModeLabelForUi(mode, this))
                                      : tr("Current audio mode: %1").arg(audioModeLabelForUi(mode, this)));
    if (m_audioActionGroup)
    {
        QSignalBlocker blocker(m_audioActionGroup);
        const QString checkedMode = pending ? mode : m_audioMode;
        for (QAction *action : m_audioActionGroup->actions())
            action->setChecked(action->data().toString() == checkedMode);
    }
    refreshStatsLabel();
    if (shouldPlaceToolbarInSidePanel())
        applyToolbarLayoutMode(true);
    else
        fitControlToolButtonWidthToText(m_audioCaptureBtn);
}

/*
 * Handles the audio button click by triggering the selected audio mode action.
 */
void ControlWindow::onAudioCaptureClicked()
{
    QAction *action = qobject_cast<QAction *>(sender());
    if (!action && m_audioActionGroup)
        action = m_audioActionGroup->checkedAction();
    onAudioModeActionTriggered(action);
}

/*
 * Requests an audio mode change from the remote side.
 */
void ControlWindow::onAudioModeActionTriggered(QAction *action)
{
    if (!action)
        return;

    const QString requestedMode = action->data().toString();
    if (requestedMode == m_audioMode && m_pendingAudioRequestId.isEmpty())
    {
        updateAudioButtonState(m_audioMode, false);
        return;
    }

    QString requestId = QUuid::createUuid().toString();
    requestId.remove(QLatin1Char('{'));
    requestId.remove(QLatin1Char('}'));
    m_pendingAudioMode = requestedMode;
    m_pendingAudioRequestId = requestId;
    updateAudioButtonState(requestedMode, true);

    QJsonObject obj = JsonUtil::createObject()
                          .add(Constant::KEY_MSGTYPE, Constant::TYPE_AUDIO_CAPTURE)
                          .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                          .add(Constant::KEY_RECEIVER, remote_id)
                          .add(Constant::KEY_RECEIVER_PWD, remote_pwd_md5)
                          .add(Constant::KEY_REQUEST_ID, requestId)
                          .add(Constant::KEY_ENABLED, requestedMode != QStringLiteral("off"))
                          .add(Constant::KEY_AUDIO_MODE, requestedMode)
                          .build();

    rtc::message_variant msgStr(JsonUtil::toCompactBytes(obj).toStdString());
    emit sendMsg2InputChannel(msgStr);
    LOG_INFO("Remote audio mode requested: mode={}, requestId={}", requestedMode, requestId);
}

/*
 * Handles the remote audio mode request result.
 */
void ControlWindow::onAudioModeRequestFinished(const QString &requestId, const QString &mode, bool accepted, const QString &message)
{
    if (!m_pendingAudioRequestId.isEmpty() && requestId != m_pendingAudioRequestId)
    {
        LOG_DEBUG("Ignoring stale audio response: requestId={}, pending={}", requestId, m_pendingAudioRequestId);
        return;
    }

    m_pendingAudioRequestId.clear();
    m_pendingAudioMode.clear();

    if (!accepted)
    {
        updateAudioButtonState(m_audioMode, false);
        if (RuntimeEnvironment::uiAvailable())
        {
            QMessageBox::information(this,
                                     tr("Audio request rejected"),
                                     message.isEmpty() ? tr("The remote side rejected audio capture.") : message);
        }
        LOG_INFO("Remote audio mode rejected: requestId={}, message={}", requestId, message);
        return;
    }

    QString normalized = mode.toLower();
    if (normalized != QStringLiteral("listen") && normalized != QStringLiteral("call"))
        normalized = QStringLiteral("off");

    m_audioMode = normalized;
    m_audioCaptureEnabled = m_audioMode != QStringLiteral("off");
    updateAudioButtonState(m_audioMode, false);

    QMetaObject::invokeMethod(&m_rtc_ctl, "setAudioMode", Qt::QueuedConnection,
                              Q_ARG(QString, m_audioMode));
    LOG_INFO("Remote audio mode accepted: mode={}, requestId={}", m_audioMode, requestId);
}
