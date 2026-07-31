/* Control window network path and display mode actions. */

#include "ui/control/control_window.h"

#include <QAction>
#include <QCoreApplication>
#include <QMetaObject>
#include <QSignalBlocker>

namespace
{
/*
 * Converts an internal network path id into user-visible text.
 */
QString networkPathLabel(const QString &path)
{
    if (path == QStringLiteral("direct"))
        return QCoreApplication::translate("ControlWindow", "Direct");
    if (path == QStringLiteral("turn_udp"))
        return QCoreApplication::translate("ControlWindow", "UDP relay");
    if (path == QStringLiteral("turn_tcp"))
        return QCoreApplication::translate("ControlWindow", "TCP relay");
    return QCoreApplication::translate("ControlWindow", "Auto");
}
} // namespace

/*
 * Requests the selected network path for later WebRTC negotiation.
 */
void ControlWindow::onNetworkPathSelected(QAction *action)
{
    if (!action)
        return;

    m_networkPath = action->data().toString();
    refreshStatsLabel();
    QMetaObject::invokeMethod(&m_rtc_ctl, "setNetworkPath", Qt::QueuedConnection,
                              Q_ARG(QString, m_networkPath));
    LOG_INFO("Network path change requested: {}", m_networkPath);
}

/*
 * Refreshes network path menu visibility, availability, and checked state from negotiation results.
 */
void ControlWindow::onNetworkPathStateChanged(const QStringList &availablePaths, const QString &selectedPath, const QString &requestedPath)
{
    if (!m_networkActionGroup)
        return;

    const QString current = selectedPath.isEmpty() ? requestedPath : selectedPath;
    const QStringList visiblePaths = availablePaths.isEmpty() ? QStringList{QStringLiteral("auto")} : availablePaths;
    QSignalBlocker blocker(m_networkActionGroup);

    for (QAction *action : m_networkActionGroup->actions())
    {
        const QString path = action->data().toString();
        const bool isAuto = path == QStringLiteral("auto");
        const bool isRequested = path == requestedPath;
        const bool isCurrent = path == current;
        const bool available = isAuto || visiblePaths.contains(path);

        action->setVisible(isAuto || available || isRequested || isCurrent);
        action->setEnabled(isAuto || available || isRequested || isCurrent);
        action->setChecked(path == (requestedPath.isEmpty() ? QStringLiteral("auto") : requestedPath));

        QString text = networkPathLabel(path);
        if (isCurrent)
            text += tr(" (current)");
        else if (available && !isAuto)
            text += tr(" (available)");
        else if (isRequested && selectedPath.isEmpty())
            text += tr(" (negotiating)");
        action->setText(text);

        QString tooltip = tr("Requested: %1, current: %2")
                              .arg(networkPathLabel(requestedPath.isEmpty() ? QStringLiteral("auto") : requestedPath),
                                   networkPathLabel(current.isEmpty() ? QStringLiteral("auto") : current));
        if (!available && !isAuto)
            tooltip += tr(", this candidate path was not discovered");
        action->setToolTip(tooltip);
    }

    LOG_INFO("Network path state updated: requested={}, selected={}, available={}",
             requestedPath, selectedPath, visiblePaths.join(","));
    refreshStatsLabel();
}

/*
 * Switches the remote video between fit-to-window and actual-size display.
 */
void ControlWindow::onDisplayModeSelected(QAction *action)
{
    if (!action)
        return;

    const QString mode = action->data().toString();
    m_fitToWindow = (mode == "fit");
    scrollArea.setWidgetResizable(m_fitToWindow);
    label.setSizePolicy(m_fitToWindow ? QSizePolicy::Ignored : QSizePolicy::Expanding, QSizePolicy::Ignored);
    scrollArea.setVerticalScrollBarPolicy(m_fitToWindow ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);
    scrollArea.setHorizontalScrollBarPolicy(m_fitToWindow ? Qt::ScrollBarAlwaysOff : Qt::ScrollBarAsNeeded);

    if (!m_fitToWindow && !m_sourcePixmap.isNull())
        label.resize(m_sourcePixmap.size());
    updateScaledPixmap();
    updateToolbarPosition();
    refreshStatsLabel();
    LOG_INFO("Display mode changed: {}", m_fitToWindow ? "fit-to-window" : "actual-size");
}
