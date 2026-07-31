#ifndef WS_CLI_H
#define WS_CLI_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QWebSocket>
#include <QAuthenticator>
#include <QNetworkProxy>
#include <QSslPreSharedKeyAuthenticator>
#include "common/constant.h"

class WsCli : public QObject
{
    Q_OBJECT
public:
    explicit WsCli(QObject *parent = nullptr);
    ~WsCli();

private:
    quint64 m_heart_interval_ms = 30000;
    QTimer *m_heart_timer = nullptr;
    QTimer *m_reconnect_timer = nullptr;
    QTimer *m_reconnect_followup_timer = nullptr;
    QWebSocket *m_ws = nullptr;
    QUrl m_url;
    bool m_connected = false;
    bool autoConnect = false;
    bool m_shutdownDone = false;

    int m_reconnect_phase = 0;
    int m_reconnect_count = 0;
    static const int MAX_RETRY_PER_PHASE = 10;

    void scheduleReconnect();
    void performShutdown();
    void destroySocket();
    static bool isSupportedSignalingUrl(const QUrl &url);

signals:
    void shutdownFinished();
    void startReconnectTimer(int msec);
    void stopReconnectTimer();
    void startHeartTimer(int msec);
    void stopHeartTimer();
    void wsClose(QWebSocketProtocol::CloseCode closeCode = QWebSocketProtocol::CloseCodeNormal,
                 const QString &reason = QString());
    void wsOpen(const QUrl &url);
    void wsPing(const QByteArray &payload = QByteArray());
    void onWsCliDisconnected();
    void onWsCliConnected();
    void onWsCliRecvTextMsg(const QString &message);
    void onWsCliRecvBinaryMsg(const QByteArray &message);
    void onReconnectStatusUpdate(const QString &status, int phase, int attempt, int nextDelaySeconds);

public slots:
    void shutdown();
    void shutdownAndMoveToOwnerThread(QObject *owner);
    void init(const QString &url, quint64 heart_interval_ms);
    void resetUrlAndReconnect(const QString &url);
    void onWsConnected();
    void onWsDisconnected();
    void onWsBinaryMessageReceived(const QByteArray &message);
    void onWsTextMessageReceived(const QString &message);
    void onWsError(QAbstractSocket::SocketError error);
    void onWsAboutToClose();
    void onWsPong(quint64 elapsedTime, const QByteArray &payload);
    void onWsPreSharedKeyAuthenticationRequired(QSslPreSharedKeyAuthenticator *authenticator);
    void onWsProxyAuthenticationRequired(const QNetworkProxy &proxy, QAuthenticator *authenticator);
    void onWsSslErrors(const QList<QSslError> &errors);

    void reConnect();
    void attemptReconnect();
    void scheduleNextReconnectIfNeeded();
    void sendWsCliTextMsg(const QString &msg);
    void sendWsCliBinaryMsg(const QByteArray &msg);
    void sendHeartMsg();
};

#endif /* WS_CLI_H */
