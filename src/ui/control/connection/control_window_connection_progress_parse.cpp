#include "ui/control/control_window.h"
#include "ui/control/connection/control_window_connection_progress_state.h"

#include "common/constant.h"

#include <QCoreApplication>

using namespace ControlConnectionProgress;

namespace
{
bool containsText(const QString &text, const QString &needle)
{
    return text.contains(needle, Qt::CaseInsensitive);
}

QString translatedStatusPrefix(const char *source)
{
    QString text = QCoreApplication::translate("WebRtcCtl", source);
    const int placeholder = text.indexOf(QStringLiteral("%1"));
    if (placeholder >= 0)
        text = text.left(placeholder);
    return text.trimmed();
}

bool containsCtlStatus(const QString &text, const char *source)
{
    const QString translated = translatedStatusPrefix(source);
    return containsText(text, QString::fromUtf8(source)) ||
           (!translated.isEmpty() && containsText(text, translated));
}

bool containsCtlTerm(const QString &text, const char *source)
{
    const QString translated = QCoreApplication::translate("WebRtcCtl", source);
    return containsText(text, QString::fromUtf8(source)) ||
           (!translated.isEmpty() && containsText(text, translated));
}
}

/*
 * Maps WebRTC status text to the visible connection progress steps.
 */
void ControlWindow::appendConnectionProgress(const QString &status)
{
    const QString trimmed = status.trimmed();
    if (trimmed.isEmpty())
        return;

    if (m_connectionStepStates.size() != kConnectionStepCount)
        resetConnectionProgress();

    const QString lower = trimmed.toLower();
    const bool failed = lower.contains(QStringLiteral("password_incorrect")) ||
                        containsText(trimmed, QStringLiteral("failed")) ||
                        containsText(trimmed, QStringLiteral("error")) ||
                        containsText(trimmed, QStringLiteral("disconnected")) ||
                        containsText(trimmed, QStringLiteral("closed")) ||
                        containsCtlTerm(trimmed, "Failed") ||
                        containsCtlTerm(trimmed, "Disconnected") ||
                        containsCtlTerm(trimmed, "Closed");
    if (failed)
    {
        QString reason = trimmed;
        const int colon = qMax(reason.lastIndexOf(QLatin1Char(':')),
                               reason.lastIndexOf(QChar(0xFF1A)));
        if (colon >= 0 && colon + 1 < reason.size())
            reason = reason.mid(colon + 1).trimmed();
        if (reason == Constant::ERROR_PASSWORD_INCORRECT ||
            reason.contains(QStringLiteral("password_incorrect"), Qt::CaseInsensitive))
        {
            reason = tr("Incorrect password");
        }
        m_connectionFailureReason = reason;
        markStepFailed(m_connectionStepStates, StepConnectionResult);
        renderConnectionProgress();
        return;
    }

    if (containsCtlStatus(trimmed, "Controller codec initialization complete: %1") ||
        (lower.contains(QStringLiteral("controller")) &&
         lower.contains(QStringLiteral("codec")) &&
         lower.contains(QStringLiteral("complete"))))
    {
        markStepDone(m_connectionStepStates, StepLocalDecoder);
        markStepRunning(m_connectionStepStates, StepConnectRequest);
    }
    else if (containsCtlStatus(trimmed, "Connection request sent to remote") ||
             (lower.contains(QStringLiteral("connection request")) &&
              lower.contains(QStringLiteral("sent"))))
    {
        markStepDone(m_connectionStepStates, StepConnectRequest);
        markStepRunning(m_connectionStepStates, StepRemoteEncoder);
    }
    else if (containsCtlStatus(trimmed, "Remote codec initialization complete: %1") ||
             containsText(trimmed, QStringLiteral("Remote encoder selected")) ||
             containsText(trimmed, QStringLiteral("Remote media parameters")) ||
             (lower.contains(QStringLiteral("remote")) &&
              lower.contains(QStringLiteral("codec")) &&
              lower.contains(QStringLiteral("complete"))) ||
             lower.contains(QStringLiteral("remote encoder")) ||
             lower.contains(QStringLiteral("remote media")))
    {
        markStepDone(m_connectionStepStates, StepRemoteEncoder);
        markStepRunning(m_connectionStepStates, StepCodecNegotiation);
    }
    else if (containsCtlStatus(trimmed, "Received remote %1") ||
             containsCtlStatus(trimmed, "Remote description set, creating answer") ||
             containsCtlStatus(trimmed, "Remote description set, continuing ICE negotiation") ||
             containsCtlStatus(trimmed, "Sent local %1, waiting for media connection...") ||
             trimmed.contains(QStringLiteral("ICE ")) ||
             lower.contains(QStringLiteral("ice ")) ||
             lower.contains(QStringLiteral("remote description")) ||
             lower.contains(QStringLiteral("local offer")) ||
             lower.contains(QStringLiteral("local answer")) ||
             lower.contains(QStringLiteral("waiting for media")))
    {
        markStepRunning(m_connectionStepStates, StepCodecNegotiation);
    }
    else if (((containsText(trimmed, QStringLiteral("connected")) ||
               containsCtlTerm(trimmed, "Connected")) &&
              !containsText(trimmed, QStringLiteral("disconnected")) &&
              !containsCtlTerm(trimmed, "Disconnected")) ||
             lower.contains(QStringLiteral("connection established")))
    {
        markStepDone(m_connectionStepStates, StepCodecNegotiation);
        markStepDone(m_connectionStepStates, StepConnectionResult);
        markStepRunning(m_connectionStepStates, StepWaitingFrame);
    }
    else if (containsCtlStatus(trimmed, "Received remote %1 track, waiting for first frame...") ||
             containsText(trimmed, QStringLiteral("waiting for first frame")) ||
             containsText(trimmed, QStringLiteral("waiting for video")))
    {
        markStepDone(m_connectionStepStates, StepConnectionResult);
        markStepRunning(m_connectionStepStates, StepWaitingFrame);
    }
    else
    {
        markStepRunning(m_connectionStepStates, StepLocalDecoder);
    }

    renderConnectionProgress();
}

/*
 * Receives WebRTC controller status changes and advances the progress view.
 */
void ControlWindow::onConnectionStatusChanged(const QString &status)
{
    appendConnectionProgress(status);
}
