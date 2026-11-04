#include "websocket/ws_cli.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QUrlQuery>
#include <QUuid>
#include <QWebSocketServer>

#include <functional>

namespace
{
bool waitUntil(const std::function<bool()> &predicate, int timeout = 3000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!predicate() && elapsed.elapsed() < timeout)
        QTest::qWait(10);
    return predicate();
}

QString latestState(const QSignalSpy &spy, const QString &id)
{
    for (int index = spy.size() - 1; index >= 0; --index)
    {
        if (spy.at(index).at(0).toString() == id)
            return spy.at(index).at(1).toString();
    }
    return QString();
}

void sendObject(QWebSocket &socket, const QJsonObject &object)
{
    socket.sendTextMessage(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
    socket.flush();
}

class Router : public QObject
{
public:
    Router()
        : server(QStringLiteral("presence-test"), QWebSocketServer::NonSecureMode)
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QWebSocketServer::newConnection, this, [this]() {
            QWebSocket *socket = server.nextPendingConnection();
            socket->setParent(this);
            const QString id = QUrlQuery(socket->requestUrl()).queryItemValue(QStringLiteral("sessionId"));
            peers.insert(id, socket);
            connect(socket, &QWebSocket::textMessageReceived, this, [this, socket, id](const QString &text) {
                QJsonObject object = QJsonDocument::fromJson(text.toUtf8()).object();
                if (object.isEmpty())
                    return;
                object.insert(QStringLiteral("sender"), id);
                if (object.value(QStringLiteral("type")).toString() == QStringLiteral("device_info_request"))
                    requests.append(object);
                QWebSocket *target = peers.value(object.value(QStringLiteral("receiver")).toString());
                if (target && target->state() == QAbstractSocket::ConnectedState)
                    sendObject(*target, object);
                else
                    sendObject(*socket, QJsonObject{
                        {QStringLiteral("sender"), QStringLiteral("server")},
                        {QStringLiteral("receiver"), id},
                        {QStringLiteral("type"), QStringLiteral("error")},
                        {QStringLiteral("role"), object.value(QStringLiteral("role"))},
                        {QStringLiteral("data"), QStringLiteral("The controlled end may not be online")}});
            });
        });
    }

    QString url(const QString &id) const
    {
        return QStringLiteral("ws://127.0.0.1:%1/?sessionId=%2").arg(server.serverPort()).arg(id);
    }

    QWebSocketServer server;
    QHash<QString, QPointer<QWebSocket>> peers;
    QList<QJsonObject> requests;
};

QJsonObject replyTo(const QJsonObject &request)
{
    return QJsonObject{
        {QStringLiteral("receiver"), request.value(QStringLiteral("sender"))},
        {QStringLiteral("type"), QStringLiteral("device_info_response")},
        {QStringLiteral("role"), request.value(QStringLiteral("role"))},
        {QStringLiteral("requestId"), request.value(QStringLiteral("requestId"))},
        {QStringLiteral("os"), QStringLiteral("Test OS")}};
}
}

class DevicePresenceTests : public QObject
{
    Q_OBJECT

private slots:
    void onlineOfflineAndErrorIsolation()
    {
        Router router;
        QVERIFY(router.server.isListening());
        WsCli controller;
        WsCli controlled;
        QSignalSpy controllerConnected(&controller, &WsCli::onWsCliConnected);
        QSignalSpy controlledConnected(&controlled, &WsCli::onWsCliConnected);
        QSignalSpy presence(&controller, &WsCli::devicePresenceChanged);
        QSignalSpy normalMessages(&controller, &WsCli::onWsCliRecvTextMsg);
        controller.init(router.url(QStringLiteral("controller")), 30000);
        controlled.init(router.url(QStringLiteral("controlled")), 30000);
        QVERIFY(waitUntil([&]() { return controllerConnected.size() == 1 && controlledConnected.size() == 1; }));

        controller.queryDevicePresence({QStringLiteral("controlled"), QStringLiteral("missing")});
        QVERIFY(waitUntil([&]() {
            return latestState(presence, QStringLiteral("controlled")) == QStringLiteral("online") &&
                   latestState(presence, QStringLiteral("missing")) == QStringLiteral("offline");
        }));
        QCOMPARE(normalMessages.size(), 0);
        bool hasOs = false;
        for (const QList<QVariant> &event : presence)
        {
            if (event.at(0).toString() == QStringLiteral("controlled") &&
                event.at(1).toString() == QStringLiteral("online"))
                hasOs = !event.at(2).toString().isEmpty();
        }
        QVERIFY(hasOs);

        controller.onWsTextMessageReceived(QStringLiteral(
            "{\"sender\":\"server\",\"receiver\":\"controller\",\"type\":\"error\",\"role\":\"ctl\",\"data\":\"normal error\"}"));
        QCOMPARE(normalMessages.size(), 1);
    }

    void freshRequestRejectsStaleOrSpoofedReplies()
    {
        Router router;
        WsCli controller;
        QWebSocket legacy;
        QSignalSpy connected(&controller, &WsCli::onWsCliConnected);
        QSignalSpy presence(&controller, &WsCli::devicePresenceChanged);
        QSignalSpy normalMessages(&controller, &WsCli::onWsCliRecvTextMsg);
        controller.init(router.url(QStringLiteral("controller")), 30000);
        legacy.open(QUrl(router.url(QStringLiteral("legacy"))));
        QVERIFY(waitUntil([&]() { return connected.size() == 1 && legacy.state() == QAbstractSocket::ConnectedState; }));
        controller.queryDevicePresence({QStringLiteral("legacy")});
        QVERIFY(waitUntil([&]() { return router.requests.size() == 1; }));
        const QJsonObject stale = router.requests.first();
        controller.queryDevicePresence({QStringLiteral("legacy")});
        controller.queryDevicePresence({QStringLiteral("legacy")});
        QVERIFY(waitUntil([&]() { return router.requests.size() == 2; }));
        const QJsonObject current = router.requests.last();
        QVERIFY(current.value(QStringLiteral("requestId")) != stale.value(QStringLiteral("requestId")));
        sendObject(legacy, replyTo(stale));
        QTest::qWait(60);
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));

        // The server overwrites the sender with legacy, so a peer cannot forge
        // a server offline reply even if it knows the outstanding role.
        sendObject(legacy, QJsonObject{
            {QStringLiteral("sender"), QStringLiteral("server")},
            {QStringLiteral("receiver"), QStringLiteral("controller")},
            {QStringLiteral("type"), QStringLiteral("error")},
            {QStringLiteral("role"), current.value(QStringLiteral("role"))},
            {QStringLiteral("data"), QStringLiteral("controlled_offline")}});
        QTest::qWait(60);
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
        sendObject(legacy, replyTo(current));
        QVERIFY(waitUntil([&]() { return latestState(presence, QStringLiteral("legacy")) == QStringLiteral("online"); }));
        QCOMPARE(normalMessages.size(), 0);

        controller.resetUrlAndReconnect(QString());
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
        sendObject(legacy, replyTo(current));
        QTest::qWait(60);
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
    }

    void legacyTimeoutStaysUnknownAndQueriesAreNotReplayed()
    {
        Router router;
        WsCli controller;
        QWebSocket legacy;
        QSignalSpy connected(&controller, &WsCli::onWsCliConnected);
        QSignalSpy presence(&controller, &WsCli::devicePresenceChanged);
        controller.queryDevicePresence({QStringLiteral("legacy")});
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
        controller.init(router.url(QStringLiteral("controller")), 30000);
        legacy.open(QUrl(router.url(QStringLiteral("legacy"))));
        QVERIFY(waitUntil([&]() { return connected.size() == 1 && legacy.state() == QAbstractSocket::ConnectedState; }));
        QTest::qWait(60);
        QCOMPARE(router.requests.size(), 0);
        controller.queryDevicePresence({QStringLiteral("legacy")});
        QVERIFY(waitUntil([&]() { return router.requests.size() == 1; }));
        const int eventsBeforeTimeout = presence.size();
        QVERIFY(waitUntil([&]() { return presence.size() > eventsBeforeTimeout; }, 9000));
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
        sendObject(legacy, replyTo(router.requests.first()));
        QTest::qWait(60);
        QCOMPARE(latestState(presence, QStringLiteral("legacy")), QStringLiteral("unknown"));
    }

    void limitsBatchAndResponderRate()
    {
        Router router;
        WsCli controller;
        WsCli lastAllowedDevice;
        QSignalSpy connected(&controller, &WsCli::onWsCliConnected);
        QSignalSpy lastAllowedConnected(&lastAllowedDevice, &WsCli::onWsCliConnected);
        QSignalSpy presence(&controller, &WsCli::devicePresenceChanged);
        controller.init(router.url(QStringLiteral("controller")), 30000);
        lastAllowedDevice.init(router.url(QStringLiteral("device-999")), 30000);
        QVERIFY(waitUntil([&]() { return connected.size() == 1 && lastAllowedConnected.size() == 1; }));
        QStringList ids;
        for (int index = 0; index < 1004; ++index)
            ids.append(QStringLiteral("device-%1").arg(index));
        controller.queryDevicePresence(ids);
        QVERIFY(waitUntil([&]() {
            return router.requests.size() == 1000 &&
                   latestState(presence, QStringLiteral("device-999")) == QStringLiteral("online");
        }));
        QCOMPARE(latestState(presence, QStringLiteral("device-1003")), QStringLiteral("unknown"));

        QWebSocket attacker;
        QSignalSpy responses(&attacker, &QWebSocket::textMessageReceived);
        attacker.open(QUrl(router.url(QStringLiteral("attacker"))));
        QVERIFY(waitUntil([&]() { return attacker.state() == QAbstractSocket::ConnectedState; }));
        for (int index = 0; index < 40; ++index)
        {
            const QString requestId = QUuid::createUuid().toString().remove(QLatin1Char('{')).remove(QLatin1Char('}'));
            sendObject(attacker, QJsonObject{
                {QStringLiteral("receiver"), QStringLiteral("controller")},
                {QStringLiteral("type"), QStringLiteral("device_info_request")},
                {QStringLiteral("requestId"), requestId},
                {QStringLiteral("role"), QStringLiteral("device_presence:") + requestId}});
        }
        QVERIFY(waitUntil([&]() { return responses.size() >= 32; }));
        QTest::qWait(100);
        QCOMPARE(responses.size(), 32);
    }
};

QTEST_GUILESS_MAIN(DevicePresenceTests)
#include "test_device_presence.moc"
