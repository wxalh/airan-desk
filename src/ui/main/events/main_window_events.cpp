#include "ui/main/main_window.h"
#include "ui/settings/window/settings_window.h"
#include "common/constant.h"
#include "util/config/config_util.h"
#include "util/input/input_util.h"
#include "util/json/json_util.h"
#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMessageBox>
#include <QResizeEvent>
#include <QScreen>
#include <QSignalBlocker>
#include <QTimer>
#include <QPainter>
#include <QMap>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <windows.h>
#include <wtsapi32.h>
#endif

namespace
{
QString formatControlledSessionDuration(qint64 elapsedMs)
{
    const qint64 totalSeconds = qMax<qint64>(0, elapsedMs / 1000);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3")
        .arg(hours, 2, 10, QLatin1Char('0'))
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString controlledSessionModeLabel(const QString &mode)
{
    if (mode == QStringLiteral("desktop"))
        return QCoreApplication::translate("MainWindow", "Desktop");
    if (mode == QStringLiteral("terminal"))
        return QCoreApplication::translate("MainWindow", "Terminal");
    if (mode == QStringLiteral("file"))
        return QCoreApplication::translate("MainWindow", "File");
    return mode;
}

QIcon makeControlledSessionTrayIcon(const QIcon &baseIcon, int count)
{
    if (count <= 0)
        return baseIcon;

    QIcon result;
    const QList<int> sizes{16, 20, 24, 32, 48, 64};
    for (int size : sizes)
    {
        QPixmap pixmap = baseIcon.pixmap(size, size);
        if (pixmap.isNull())
        {
            pixmap = QPixmap(size, size);
            pixmap.fill(Qt::transparent);
        }

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const int diameter = qMax(10, size / 2);
        const QRect badgeRect(size - diameter, 0, diameter, diameter);
        painter.setPen(QPen(Qt::white, qMax(1, size / 24)));
        painter.setBrush(QColor(211, 47, 47));
        painter.drawEllipse(badgeRect.adjusted(1, 1, -1, -1));

        QFont font = painter.font();
        font.setBold(true);
        font.setPixelSize(qMax(6, diameter / 2));
        painter.setFont(font);
        painter.setPen(Qt::white);
        painter.drawText(badgeRect, Qt::AlignCenter,
                         count > 9 ? QStringLiteral("9+") : QString::number(count));
        painter.end();
        result.addPixmap(pixmap);
    }
    return result;
}

QIcon makeEmptyControlledSessionTrayIcon()
{
    QIcon result;
    const QList<int> sizes{16, 20, 24, 32, 48, 64};
    for (int size : sizes)
    {
        QPixmap pixmap(size, size);
        pixmap.fill(Qt::transparent);
        result.addPixmap(pixmap);
    }
    return result;
}
} /* namespace */

void MainWindow::initTray()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        LOG_WARN("System tray is not available");
#if defined(Q_OS_LINUX)
        static int retryCount = 0;
        if (retryCount < 12)
        {
            ++retryCount;
            QTimer::singleShot(5000, this, &MainWindow::initTray);
        }
#endif
        return;
    }

    if (m_trayIcon)
        return;

    m_trayMenu = new QMenu(this);
    m_trayStatusAction = m_trayMenu->addAction(tr("Remote connection active"));
    m_trayStatusAction->setEnabled(false);
    m_trayStatusAction->setVisible(false);
    m_trayConnectionsMenu = m_trayMenu->addMenu(tr("Remote connections"));
    m_trayConnectionsMenu->menuAction()->setVisible(false);
    m_trayDisconnectAllAction = m_trayMenu->addAction(tr("Disconnect all"), this, &MainWindow::disconnectAllControlledSessions);
    m_trayDisconnectAllAction->setVisible(false);
    m_trayMenu->addSeparator();
    m_trayOpenAction = m_trayMenu->addAction(tr("Open window"), this, &MainWindow::showWindowFromTray);
    m_traySettingsAction = m_trayMenu->addAction(tr("Settings"), this, &MainWindow::openSettingsFromTray);
    m_trayMenu->addSeparator();
    m_trayQuitAction = m_trayMenu->addAction(tr("Quit"), this, &MainWindow::quitFromTray);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setToolTip(windowTitle);
    m_trayIcon->setContextMenu(m_trayMenu);
    m_trayBaseIcon = qApp->windowIcon();
    m_trayEmptyIcon = makeEmptyControlledSessionTrayIcon();
    m_trayIcon->setIcon(m_trayBaseIcon);
    m_traySessionClock.start();
    m_traySessionTimer = new QTimer(this);
    m_traySessionTimer->setInterval(500);
    connect(m_traySessionTimer, &QTimer::timeout,
            this, &MainWindow::updateControlledSessionTrayPresentation);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason)
            {
        if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
            showWindowFromTray(); });
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::cleanupTray, Qt::DirectConnection);
    m_trayIcon->show();
}

void MainWindow::cleanupTray()
{
    if (!m_trayIcon)
        return;

    m_trayIcon->hide();
    m_trayIcon->setContextMenu(nullptr);
    delete m_trayIcon;
    m_trayIcon = nullptr;
    if (m_traySessionTimer)
        m_traySessionTimer->stop();
    m_controlledTraySessions.clear();
}

void MainWindow::initDesktopScreenChangeMonitor()
{
    QGuiApplication *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!guiApp)
        return;

    if (!m_desktopScreenChangeTimer)
    {
        m_desktopScreenChangeTimer = new QTimer(this);
        m_desktopScreenChangeTimer->setSingleShot(true);
        connect(m_desktopScreenChangeTimer, &QTimer::timeout, this, [this]() {
            int notified = 0;
            for (auto it = m_rtcCliSessions.begin(); it != m_rtcCliSessions.end(); ++it)
            {
                WebRtcCli *cli = it.key();
                if (!cli)
                    continue;
                QMetaObject::invokeMethod(cli, "handleDesktopScreensChanged", Qt::QueuedConnection);
                ++notified;
            }
            LOG_DEBUG("Desktop screen change notification dispatched after debounce: sessions={}", notified);
        });
    }

    const QList<QScreen *> screens = guiApp->screens();
    for (QScreen *screen : screens)
        bindDesktopScreenChangeSignals(screen);

    connect(guiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
        bindDesktopScreenChangeSignals(screen);
        notifyDesktopScreensChanged();
    });
    connect(guiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *) {
        m_boundDesktopScreens.clear();
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (QScreen *screen : screens)
            bindDesktopScreenChangeSignals(screen);
        notifyDesktopScreensChanged();
    });
    connect(guiApp, &QGuiApplication::primaryScreenChanged, this, [this](QScreen *) {
        notifyDesktopScreensChanged();
    });
}

void MainWindow::bindDesktopScreenChangeSignals(QScreen *screen)
{
    if (!screen || m_boundDesktopScreens.contains(screen))
        return;
    m_boundDesktopScreens.insert(screen);
    connect(screen, &QScreen::geometryChanged, this, [this](const QRect &) {
        notifyDesktopScreensChanged();
    });
    connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this](qreal) {
        notifyDesktopScreensChanged();
    });
    connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this](qreal) {
        notifyDesktopScreensChanged();
    });
}

void MainWindow::notifyDesktopScreensChanged()
{
    if (m_desktopScreenChangeTimer)
        m_desktopScreenChangeTimer->start(800);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_trayIcon && m_trayIcon->isVisible())
    {
        hide();
        event->ignore();
        LOG_INFO("Main window hidden to system tray");
        return;
    }

    QWidget::closeEvent(event);
    qApp->quit();
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_trayIcon && m_trayIcon->isVisible())
    {
        hide();
        event->accept();
        LOG_INFO("Main window hidden to system tray by Escape");
        return;
    }

    QWidget::keyPressEvent(event);
}

bool MainWindow::event(QEvent *event)
{
    const bool handled = QWidget::event(event);
    if (!event)
        return handled;

    switch (event->type())
    {
    case QEvent::ApplicationFontChange:
    case QEvent::StyleChange:
    case QEvent::ScreenChangeInternal:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    case QEvent::DevicePixelRatioChange:
#endif
        scheduleMainLayoutRefresh();
        break;
    default:
        break;
    }
    return handled;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    constexpr double kMainAspect = 800.0 / 638.0;
    if (event && !event->size().isEmpty())
    {
        const int expectedHeight = qMax(minimumHeight(), static_cast<int>(qRound(width() / kMainAspect)));
        if (qAbs(expectedHeight - height()) > 1)
        {
            QSignalBlocker blocker(this);
            resize(width(), expectedHeight);
            return;
        }
    }
    if (m_content)
        layoutMainContent();
}

void MainWindow::scheduleMainLayoutRefresh()
{
    if (!m_content)
        return;

    const auto refresh = [this]() {
        if (m_content)
            layoutMainContent();
    };
    QTimer::singleShot(0, this, refresh);
    QTimer::singleShot(120, this, refresh);
}

void MainWindow::showWindowFromTray()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    showNormal();
    raise();
    activateWindow();
}

void MainWindow::openSettingsFromTray()
{
    if (!RuntimeEnvironment::uiAvailable())
        return;

    if (m_settingsWindow)
    {
        m_settingsWindow->show();
        m_settingsWindow->raise();
        m_settingsWindow->activateWindow();
        return;
    }

    m_settingsWindow = new SettingsWindow(this);
    m_settingsWindow->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_settingsWindow, &QObject::destroyed, this, [this]()
            { m_settingsWindow = nullptr; });
    connect(m_settingsWindow, &SettingsWindow::controlledAccessChanged, this, [this](bool allowed) {
        if (!allowed)
            disconnectAllControlledSessions();
    });
    connect(m_settingsWindow, &SettingsWindow::signalingServerChanged, this, [this]() {
        applySignalingConfiguration(true);
    });
    m_settingsWindow->show();
    m_settingsWindow->raise();
    m_settingsWindow->activateWindow();
}

void MainWindow::quitFromTray()
{
    cleanupTray();
    qApp->quit();
}

void MainWindow::onControlledSessionConnected(const QString &sessionId,
                                              const QString &peerId,
                                              const QString &mode,
                                              const QString &sourceIp)
{
    WebRtcCli *session = qobject_cast<WebRtcCli *>(sender());
    if (!session || sessionId.isEmpty())
        return;
    const bool isNewSession = !m_controlledTraySessions.contains(sessionId);
    ControlledTraySession entry;
    entry.session = session;
    entry.sessionId = sessionId;
    entry.peerId = peerId;
    entry.mode = mode;
    entry.sourceIp = sourceIp;
    entry.connectedSinceMs = m_traySessionClock.isValid() ? m_traySessionClock.elapsed() : 0;
    m_controlledTraySessions.insert(sessionId, entry);
    updateControlledSessionTray();
    if (isNewSession && m_trayIcon)
    {
        m_trayIcon->showMessage(
            tr("New remote connection"),
            tr("%1 connected (%2) from %3.")
                .arg(peerId, controlledSessionModeLabel(mode), sourceIp),
            QSystemTrayIcon::Information,
            5000);
    }
}

void MainWindow::onControlledSessionDisconnected(const QString &sessionId,
                                                  const QString &peerId,
                                                  const QString &reason)
{
    m_controlledTraySessions.remove(sessionId);
    updateControlledSessionTray();
    if (reason == QStringLiteral("controller_user") && m_trayIcon)
    {
        m_trayIcon->showMessage(
            tr("Remote connection ended"),
            tr("Controller %1 disconnected this session.").arg(peerId),
            QSystemTrayIcon::Information,
            5000);
    }
}

void MainWindow::updateControlledSessionTray()
{
    if (!m_trayIcon || !m_trayConnectionsMenu)
        return;
    for (auto it = m_controlledTraySessions.begin(); it != m_controlledTraySessions.end();)
    {
        if (it.value().session.isNull())
            it = m_controlledTraySessions.erase(it);
        else
            ++it;
    }

    m_trayConnectionsMenu->clear();
    const int count = m_controlledTraySessions.size();
    const bool active = count > 0;
    if (active)
    {
        m_trayConnectedIcon = makeControlledSessionTrayIcon(m_trayBaseIcon, count);
        m_trayAttentionPhase = false;
    }
    m_trayStatusAction->setText(tr("Remote connection active (%1)").arg(count));
    m_trayStatusAction->setVisible(active);
    m_trayConnectionsMenu->menuAction()->setVisible(active);
    m_trayDisconnectAllAction->setVisible(active);
    QMap<QString, QStringList> sessionIdsByPeer;
    for (auto it = m_controlledTraySessions.constBegin(); it != m_controlledTraySessions.constEnd(); ++it)
        sessionIdsByPeer[it.value().peerId].append(it.key());
    m_trayIcon->setToolTip(
        active ? tr("Being controlled remotely... (%1)").arg(sessionIdsByPeer.size())
               : windowTitle);

    for (auto peerIt = sessionIdsByPeer.begin(); peerIt != sessionIdsByPeer.end(); ++peerIt)
    {
        QMultiMap<qint64, QString> sessionIdsByConnectedTime;
        for (const QString &sessionId : peerIt.value())
            sessionIdsByConnectedTime.insert(
                m_controlledTraySessions.value(sessionId).connectedSinceMs,
                sessionId);
        const QStringList sessionIds = sessionIdsByConnectedTime.values();

        QMenu *peerMenu = m_trayConnectionsMenu->addMenu(
            tr("%1 (%2 connections)").arg(peerIt.key()).arg(sessionIds.size()));
        for (const QString &sessionId : sessionIds)
        {
            ControlledTraySession &entry = m_controlledTraySessions[sessionId];
            QAction *action = peerMenu->addAction(
                tr("Disconnect %1 - %2")
                    .arg(controlledSessionModeLabel(entry.mode),
                         formatControlledSessionDuration(
                             m_traySessionClock.elapsed() - entry.connectedSinceMs)));
            entry.action = action;
            action->setData(sessionId);
            connect(action, &QAction::triggered,
                    this, &MainWindow::disconnectControlledSessionFromAction);
        }
        peerMenu->addSeparator();
        QAction *disconnectPeerAction = peerMenu->addAction(
            tr("Disconnect all from %1").arg(peerIt.key()));
        disconnectPeerAction->setData(peerIt.key());
        connect(disconnectPeerAction, &QAction::triggered, this,
                &MainWindow::disconnectControlledPeerFromAction);
    }

    if (active && m_traySessionTimer && !m_traySessionTimer->isActive())
        m_traySessionTimer->start();
    if (!active && m_traySessionTimer)
    {
        m_traySessionTimer->stop();
        m_trayAttentionPhase = false;
        m_trayIcon->setIcon(m_trayBaseIcon);
    }
    updateControlledSessionTrayPresentation();
}

void MainWindow::updateControlledSessionTrayPresentation()
{
    if (!m_trayIcon)
        return;

    const int count = m_controlledTraySessions.size();
    if (count <= 0)
    {
        m_trayAttentionPhase = false;
        m_trayIcon->setIcon(m_trayBaseIcon);
        return;
    }

    m_trayIcon->setIcon(
        m_trayAttentionPhase ? m_trayEmptyIcon : m_trayConnectedIcon);
    m_trayAttentionPhase = !m_trayAttentionPhase;
    const qint64 now = m_traySessionClock.elapsed();
    for (auto it = m_controlledTraySessions.begin(); it != m_controlledTraySessions.end(); ++it)
    {
        ControlledTraySession &entry = it.value();
        if (!entry.action)
            continue;
        entry.action->setText(
            tr("Disconnect %1 - %2")
                .arg(controlledSessionModeLabel(entry.mode),
                     formatControlledSessionDuration(now - entry.connectedSinceMs)));
    }
}

void MainWindow::disconnectControlledSessionFromAction()
{
    const QAction *action = qobject_cast<QAction *>(sender());
    if (!action)
        return;
    const QString sessionId = action->data().toString();
    const auto entryIt = m_controlledTraySessions.constFind(sessionId);
    if (entryIt != m_controlledTraySessions.constEnd() && entryIt->session)
        QMetaObject::invokeMethod(entryIt->session.data(),
                                  "requestLocalDisconnect",
                                  Qt::QueuedConnection);
}

void MainWindow::disconnectControlledPeerFromAction()
{
    const QAction *action = qobject_cast<QAction *>(sender());
    if (action)
        disconnectControlledPeer(action->data().toString());
}

void MainWindow::sendPeerDisconnect(const QString &peerId)
{
    if (peerId.isEmpty())
        return;
    const QJsonObject message = JsonUtil::createObject()
                                    .add(Constant::KEY_ROLE, Constant::ROLE_CLI)
                                    .add(Constant::KEY_TYPE, Constant::TYPE_PEER_DISCONNECT)
                                    .add(Constant::KEY_SENDER, ConfigUtil->local_id)
                                    .add(Constant::KEY_RECEIVER, peerId)
                                    .add(Constant::KEY_REASON, QStringLiteral("controlled_user"))
                                    .build();
    emit sendWsCliTextMsg(JsonUtil::toCompactString(message));
}

void MainWindow::disconnectControlledPeer(const QString &peerId)
{
    sendPeerDisconnect(peerId);
    const auto sessions = m_controlledTraySessions;
    for (const ControlledTraySession &entry : sessions)
    {
        if (entry.peerId == peerId && entry.session)
        {
            QMetaObject::invokeMethod(entry.session.data(),
                                      "requestDisconnect",
                                      Qt::QueuedConnection,
                                      Q_ARG(QString, QStringLiteral("controlled_user")));
        }
    }
}

void MainWindow::disconnectAllControlledSessions()
{
    QSet<QString> peers;
    for (const ControlledTraySession &entry : m_controlledTraySessions)
        peers.insert(entry.peerId);
    for (const QString &peerId : peers)
        disconnectControlledPeer(peerId);
}

void MainWindow::updateDesktopStateForSessions(bool locked, const QString &message)
{
    m_desktopLocked = locked;
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    InputUtil::setWindowsSessionLocked(locked);
    if (locked && InputUtil::isWindowsUnattendedInputInstalled())
    {
        LOG_DEBUG("Windows session locked; Airan service is installed, keeping remote video active for secure desktop fallback");
    }
#endif
    for (auto it = m_rtcCliSessions.begin(); it != m_rtcCliSessions.end(); ++it)
    {
        WebRtcCli *cli = it.key();
        if (!cli)
            continue;

        QMetaObject::invokeMethod(cli, "setDesktopLocked", Qt::QueuedConnection, Q_ARG(bool, locked));
        if (!locked)
        {
            QMetaObject::invokeMethod(cli,
                                      "handleDesktopScreensChanged",
                                      Qt::QueuedConnection);
        }
    }

    LOG_INFO("Windows session desktop state changed: locked={}, message={}", locked, message);
}

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
#else
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
#endif
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    MSG *msg = static_cast<MSG *>(message);
    if (!msg || msg->message != WM_WTSSESSION_CHANGE)
        return false;

    if (msg->wParam == WTS_SESSION_LOCK)
    {
        updateDesktopStateForSessions(true, QStringLiteral("locked"));
    }
    else if (msg->wParam == WTS_SESSION_UNLOCK)
    {
        updateDesktopStateForSessions(false, QStringLiteral("unlocked"));
    }
    return false;
}
#endif
