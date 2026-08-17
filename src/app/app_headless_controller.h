#ifndef APP_HEADLESS_CONTROLLER_H
#define APP_HEADLESS_CONTROLLER_H

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>

#include "webrtc/cli/webrtc_cli.h"
#include "websocket/ws_cli.h"

struct ControlledAccessDecision;


class HeadlessController : public QObject
{
    Q_OBJECT
public:
    
    explicit HeadlessController(QObject *parent = nullptr);

    
    ~HeadlessController() override;

private:
    QString buildWsUrl() const;
    void cleanupWebRtcCliSessions();
    void handleDeviceIdConflict(const QJsonObject &object);
    void onWsCliRecvBinaryMsg(const QByteArray &message);
    bool handleIncomingConnectRequest(const QString &sender, const QJsonObject &object);
    void completeIncomingConnectRequest(const QString &sender,
                                        const QJsonObject &object,
                                        bool notificationDelivered);
    void startAuthorizedIncomingSession(const QString &sender,
                                        const QJsonObject &object,
                                        const ControlledAccessDecision &decision);
    void sendIncomingConnectError(const QString &sender, const QString &reason, const QString &sessionId);
    void destroyWebRtcCli(WebRtcCli *webrtcCli);

private slots:
    void handleAuditFailure(const QString &reason);
    void onControlledSessionConnected(const QString &sessionId,
                                      const QString &peerId,
                                      const QString &mode,
                                      const QString &sourceIp);
    void onControlledSessionDisconnected(const QString &sessionId,
                                         const QString &peerId,
                                         const QString &reason);
    void onDestroyWebRtcCli();

private:

    WsCli *m_ws{nullptr};
    QThread *m_wsThread{nullptr};
    QHash<WebRtcCli *, QThread *> m_rtcCliSessions;
    QSet<WebRtcCli *> m_rtcCliShutdownPending;
    bool m_shuttingDown{false};
};

#endif /* APP_HEADLESS_CONTROLLER_H */
