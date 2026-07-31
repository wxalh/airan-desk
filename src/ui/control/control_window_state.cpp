/* Control window status bar and remote media state display. */

#include "ui/control/control_window.h"
#include "ui/control/control_window_view_helpers.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QLabel>
#include <QSignalBlocker>
#include <QTimer>

namespace
{
QString checkedActionText(QActionGroup *group, const QString &fallback)
{
    if (!group || !group->checkedAction())
        return fallback;
    QString text = group->checkedAction()->text();
    text.remove(QChar('&'));
    const int marker = text.indexOf(QStringLiteral(" ("));
    if (marker > 0)
        text = text.left(marker);
    return text.trimmed().isEmpty() ? fallback : text.trimmed();
}

QString audioModeText(const QString &mode, bool pending, const QString &pendingMode, QObject *context)
{
    Q_UNUSED(context);
    const QString effective = pending ? pendingMode : mode;
    QString text;
    if (effective == QStringLiteral("listen"))
        text = QCoreApplication::translate("ControlWindow", "Listen");
    else if (effective == QStringLiteral("call"))
        text = QCoreApplication::translate("ControlWindow", "Call");
    else
        text = QCoreApplication::translate("ControlWindow", "Off");

    if (pending)
        text += QCoreApplication::translate("ControlWindow", " (requesting)");
    return text;
}
} // namespace

void ControlWindow::refreshStatsLabel()
{
    if (!m_statsLabel)
        return;

    const QString resolution = m_remoteResolution.isValid()
                                   ? QString("%1x%2").arg(m_remoteResolution.width()).arg(m_remoteResolution.height())
                                   : QString("--");
    const QString codec = m_remoteVideoCodec.isEmpty() ? QStringLiteral("--") : m_remoteVideoCodec;
    const QString capture = m_remoteCaptureMethod.isEmpty() ? QStringLiteral("--") : m_remoteCaptureMethod;
    const QString encoder = m_remoteEncoderName.isEmpty() ? QStringLiteral("--") : m_remoteEncoderName;

    const QString audio = audioModeText(m_audioMode,
                                        !m_pendingAudioRequestId.isEmpty(),
                                        m_pendingAudioMode,
                                        this);
    const QString network = checkedActionText(m_networkActionGroup, tr("Auto"));
    const QString display = checkedActionText(m_displayActionGroup, tr("Fit to window"));
    const QString transferPart = m_transferStatusText.isEmpty()
                                     ? QString()
                                     : QStringLiteral(" | ") + tr("Transfer: %1").arg(m_transferStatusText);

    m_statsLabel->setText(tr("FPS: %1 | Kbps: %2 | Resolution: %3 | Video: %4\nCapture: %5 | Encoder: %6 | Audio: %7 | Network: %8 | Display: %9%10")
                              .arg(m_currentFps, 0, 'f', 1)
                              .arg(m_currentKbps, 0, 'f', 0)
                              .arg(resolution)
                              .arg(codec)
                              .arg(capture)
                              .arg(encoder)
                              .arg(audio)
                              .arg(network)
                              .arg(display)
                              .arg(transferPart));
    if (!m_toolbarInSidePanel)
    {
        QFontMetrics statsFm(m_statsLabel->font());
        const QStringList lines = m_statsLabel->text().split(QLatin1Char('\n'));
        int lineWidth = 0;
        for (const QString &line : lines)
            lineWidth = qMax(lineWidth, controlWindowTextWidth(statsFm, line));
        m_statsLabel->setMinimumWidth(qMax(220, lineWidth + 24));
    }
}

void ControlWindow::clearTransferStatus()
{
    m_transferStatusText.clear();
    refreshStatsLabel();
}
