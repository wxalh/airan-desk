#include "ws_cli.h"

#include <QMetaObject>

namespace
{
constexpr quint64 kMaxWebSocketMessageBytes = 8ULL * 1024 * 1024;
}


bool WsCli::isSupportedSignalingUrl(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty())
        return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("ws") || scheme == QStringLiteral("wss");
}


WsCli::WsCli(QObject *parent)
    : QObject{parent},
      m_heart_interval_ms(30000),
      m_ws(nullptr),
      m_connected(false),
      autoConnect(false),
      m_reconnect_phase(0),
      m_reconnect_count(0)
{
    m_reconnect_timer = new QTimer(this);
    m_reconnect_timer->setSingleShot(true);
    m_reconnect_followup_timer = new QTimer(this);
    m_reconnect_followup_timer->setSingleShot(true);
    m_heart_timer = new QTimer(this);
    connect(this, SIGNAL(startReconnectTimer(int)), m_reconnect_timer, SLOT(start(int)));
    connect(this, SIGNAL(stopReconnectTimer()), m_reconnect_timer, SLOT(stop()));
    connect(this, SIGNAL(stopReconnectTimer()), m_reconnect_followup_timer, SLOT(stop()));
    connect(m_reconnect_timer, &QTimer::timeout, this, &WsCli::attemptReconnect);
    connect(m_reconnect_followup_timer, &QTimer::timeout,
            this, &WsCli::scheduleNextReconnectIfNeeded);

    connect(m_heart_timer, &QTimer::timeout, this, &WsCli::sendHeartMsg);
    connect(this, SIGNAL(startHeartTimer(int)), m_heart_timer, SLOT(start(int)));
    connect(this, SIGNAL(stopHeartTimer()), m_heart_timer, SLOT(stop()));
}


WsCli::~WsCli()
{
    if (!m_shutdownDone && QThread::currentThread() == thread())
        performShutdown();
}


void WsCli::performShutdown()
{
    if (m_shutdownDone)
        return;

    m_shutdownDone = true;
    emit stopHeartTimer();
    emit stopReconnectTimer();
    m_connected = false;

    destroySocket();
}

void WsCli::destroySocket()
{
    if (!m_ws)
        return;

    try
    {
        m_ws->abort();
        m_ws->close();
    }
    catch (...)
    {
    }

    m_ws->disconnect(this);
    delete m_ws;
    m_ws = nullptr;
}

void WsCli::shutdown()
{
    performShutdown();
    emit shutdownFinished();
    disconnect();
}

void WsCli::shutdownAndMoveToOwnerThread(QObject *owner)
{
    performShutdown();
    if (owner)
        moveToThread(owner->thread());
    emit shutdownFinished();
    disconnect();
}


void WsCli::init(const QString &url, quint64 heart_interval_ms)
{
    m_heart_interval_ms = heart_interval_ms;
    m_url = QUrl(url.trimmed(), QUrl::StrictMode);
    m_connected = false;
    m_reconnect_phase = 0;
    m_reconnect_count = 0;
    destroySocket();

    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_WARN("Signaling URL is not configured or invalid; WebSocket remains offline");
        return;
    }

    m_ws = new QWebSocket();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    m_ws->setMaxAllowedIncomingMessageSize(kMaxWebSocketMessageBytes);
#endif

    connect(this, &WsCli::wsClose, m_ws, &QWebSocket::close);
    connect(this, SIGNAL(wsOpen(QUrl)), m_ws, SLOT(open(QUrl)));
    connect(this, &WsCli::wsPing, m_ws, &QWebSocket::ping);

    connect(m_ws, &QWebSocket::aboutToClose, this, &WsCli::onWsAboutToClose);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WsCli::onWsBinaryMessageReceived);
    connect(m_ws, &QWebSocket::connected, this, &WsCli::onWsConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WsCli::onWsDisconnected);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_ws, &QWebSocket::errorOccurred, this, &WsCli::onWsError);
#else
    connect(m_ws, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(onWsError(QAbstractSocket::SocketError)));
#endif
    connect(m_ws, &QWebSocket::pong, this, &WsCli::onWsPong);
    connect(m_ws, &QWebSocket::preSharedKeyAuthenticationRequired, this, &WsCli::onWsPreSharedKeyAuthenticationRequired);
    connect(m_ws, &QWebSocket::proxyAuthenticationRequired, this, &WsCli::onWsProxyAuthenticationRequired);
    connect(m_ws, &QWebSocket::sslErrors, this, &WsCli::onWsSslErrors);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WsCli::onWsTextMessageReceived);

    LOG_INFO("Opening configured signaling connection");
    emit wsOpen(m_url);
}


void WsCli::resetUrlAndReconnect(const QString &url)
{
    m_url = QUrl(url.trimmed(), QUrl::StrictMode);
    m_connected = false;
    m_reconnect_phase = 0;
    m_reconnect_count = 0;
    emit stopHeartTimer();
    emit stopReconnectTimer();

    if (!isSupportedSignalingUrl(m_url))
    {
        LOG_WARN("Signaling URL is not configured or invalid; WebSocket remains offline");
        if (m_ws)
            m_ws->abort();
        return;
    }

    if (!m_ws)
    {
        init(url, m_heart_interval_ms);
        return;
    }

    LOG_INFO("Signaling URL updated; reconnecting");
    m_ws->abort();
    m_ws->open(m_url);
}
