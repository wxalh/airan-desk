#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QWidget>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QCloseEvent>
#include <QFont>
#include <QRect>
#include <QSet>
#include <QPointer>
#include <QElapsedTimer>
#include <QIcon>

#include <atomic>
#include <memory>

#include "websocket/ws_cli.h"
#include "webrtc/cli/webrtc_cli.h"

class QPushButton;
class QEvent;
class QKeyEvent;
class QLabel;
class QLineEdit;
class QRadioButton;
class QScreen;
class QTimer;
class QWidget;
class AppTitleBar;
class QCloseEvent;
class SettingsWindow;
struct ControlledAccessDecision;
struct MainWindowLayoutMetrics;

namespace Ui
{
    class MainWindow;
}

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    
    void initUI();
    
    void initCli();
    
    void connFileMgr(const QString &remote_id, const QString &remote_pwd_md5);
    
    void connDesktopMgr(const QString &remote_id, const QString &remote_pwd_md5);
    void connTerminalMgr(const QString &remote_id, const QString &remote_pwd_md5);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool event(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#else
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif
#endif
signals:
    void closeWsCli();
    void initWsCli(const QString &url, quint64 heart_interval_ms);
    void resetWsCliUrl(const QString &url);
    void sendWsCliTextMsg(const QString &message);
    void sendWsCliBinaryMsg(const QByteArray &message);
    void initRtcCli();
private slots:
    
    void on_btn_conn_clicked();
    
    void on_local_pwd_change_clicked();
    
    void on_local_share_clicked();
    
    void onWsCliConnected();
    
    void onWsCliDisconnected();
    
    void onWsCliReconnectStatus(const QString &status, int phase, int attempt, int nextDelaySeconds);
    
    void onWsCliRecvTextMsg(const QString &message);
    
    void onWsCliRecvBinaryMsg(const QByteArray &message);

    void onDestroyWebRtcCli();
    void showWindowFromTray();
    void openSettingsFromTray();
    void quitFromTray();
    void disconnectAllControlledSessions();
    void disconnectControlledSessionFromAction();
    void disconnectControlledPeerFromAction();
    void onControlledSessionConnected(const QString &sessionId,
                                      const QString &peerId,
                                      const QString &mode,
                                      const QString &sourceIp);
    void onControlledSessionDisconnected(const QString &sessionId,
                                         const QString &peerId,
                                         const QString &reason);
    void handleAuditFailure(const QString &reason);
private:
    QString buildWsUrl() const;
    void refreshSignalingStatus();
    void applySignalingConfiguration(bool reconnect);
    void handleDeviceIdConflict(const QJsonObject &object);
    bool handleIncomingConnectRequest(const QString &sender, const QJsonObject &object);
    void completeIncomingConnectRequest(const QString &sender,
                                        const QJsonObject &object,
                                        ControlledAccessDecision decision);
    QString localizedErrorMessage(const QString &message) const;
    void cleanupWebRtcCliSessions();
    void destroyWebRtcCli(WebRtcCli *webrtcCli);
    void initTray();
    void cleanupTray();
    void bindUiObjects();
    void applyMainScale();
    void layoutMainContent();
    void scheduleMainLayoutRefresh();
    MainWindowLayoutMetrics calculateMainLayoutMetrics();
    void styleMainActionButtons(const MainWindowLayoutMetrics &metrics);
    void layoutMainSectionLabels(MainWindowLayoutMetrics &metrics);
    void layoutMainTextFields(const MainWindowLayoutMetrics &metrics);
    void layoutMainModeRadios(MainWindowLayoutMetrics &metrics);
    void layoutMainActionButtons(const MainWindowLayoutMetrics &metrics);
    bool tryFillRemoteFieldsFromShareText(const QString &text);
    int scaled(int value) const;
    QRect scaledRect(int x, int y, int width, int height) const;
    QFont scaledFont(double pointSize, bool bold = false) const;
    void updateDesktopStateForSessions(bool locked, const QString &message);
    void initDesktopScreenChangeMonitor();
    void bindDesktopScreenChangeSignals(QScreen *screen);
    void notifyDesktopScreensChanged();
    void updateControlledSessionTray();
    void updateControlledSessionTrayPresentation();
    void sendPeerDisconnect(const QString &peerId);
    void disconnectControlledPeer(const QString &peerId);

    struct ControlledTraySession
    {
        QPointer<WebRtcCli> session;
        QString sessionId;
        QString peerId;
        QString mode;
        QString sourceIp;
        qint64 connectedSinceMs{0};
        QPointer<QAction> action;
    };

    QWidget *m_content{nullptr};
    AppTitleBar *m_titleBar{nullptr};
    QLabel *m_allowControlLabel{nullptr};
    QLabel *m_localIdLabel{nullptr};
    QLabel *m_localPwdLabel{nullptr};
    QLabel *m_remoteControlLabel{nullptr};
    QLabel *m_remoteIdLabel{nullptr};
    QLabel *m_remotePwdLabel{nullptr};
    QLabel *m_wsConnectStatus{nullptr};
    QLabel *m_versionLabel{nullptr};
    QLineEdit *m_localIdEdit{nullptr};
    QLineEdit *m_localPwdEdit{nullptr};
    QLineEdit *m_remoteIdEdit{nullptr};
    QLineEdit *m_remotePwdEdit{nullptr};
    QWidget *m_localIdBorder{nullptr};
    QWidget *m_localPwdBorder{nullptr};
    QWidget *m_remoteIdBorder{nullptr};
    QWidget *m_remotePwdBorder{nullptr};
    QRadioButton *m_remoteDesktopRadio{nullptr};
    QRadioButton *m_remoteFileRadio{nullptr};
    QRadioButton *m_remoteTerminalRadio{nullptr};
    QPushButton *m_connectButton{nullptr};
    QPushButton *m_localPwdChangeButton{nullptr};
    QPushButton *m_localShareButton{nullptr};
    QPushButton *m_settingsButton{nullptr};
    QWidget *m_connectDivider{nullptr};
    QWidget *m_localPwdChangeDivider{nullptr};
    QWidget *m_localShareDivider{nullptr};

    QString windowTitle;
    QString textToCopy;
    WsCli *m_ws{nullptr};
    QThread *m_ws_thread{nullptr};
    QHash<WebRtcCli *, QThread *> m_rtcCliSessions;
    QSet<WebRtcCli *> m_rtcCliShutdownPending;
    std::shared_ptr<std::atomic_bool> m_asyncCallbacksAlive{
        std::make_shared<std::atomic_bool>(true)};
    std::shared_ptr<std::atomic_int> m_pendingAccessEvaluations{
        std::make_shared<std::atomic_int>(0)};
    std::shared_ptr<std::atomic_ullong> m_accessPolicyGeneration{
        std::make_shared<std::atomic_ullong>(1)};
    std::shared_ptr<std::atomic_ullong> m_accessQueueOverflowCount{
        std::make_shared<std::atomic_ullong>(0)};
    std::shared_ptr<std::atomic_bool> m_accessQueueOverflowAuditScheduled{
        std::make_shared<std::atomic_bool>(false)};
    QSet<QScreen *> m_boundDesktopScreens;
    QTimer *m_desktopScreenChangeTimer{nullptr};
    bool isCaptureing;
    bool m_remoteShareParsing{false};
    bool m_desktopLocked{false};
    QSystemTrayIcon *m_trayIcon{nullptr};
    QMenu *m_trayMenu{nullptr};
    QAction *m_trayOpenAction{nullptr};
    QAction *m_traySettingsAction{nullptr};
    QAction *m_trayQuitAction{nullptr};
    QAction *m_trayStatusAction{nullptr};
    QAction *m_trayDisconnectAllAction{nullptr};
    QMenu *m_trayConnectionsMenu{nullptr};
    QHash<QString, ControlledTraySession> m_controlledTraySessions;
    QTimer *m_traySessionTimer{nullptr};
    QElapsedTimer m_traySessionClock;
    QIcon m_trayBaseIcon;
    QIcon m_trayConnectedIcon;
    SettingsWindow *m_settingsWindow{nullptr};
    double m_uiScale{1.0};
    double m_dpiScale{1.0};
    Ui::MainWindow *ui{nullptr};
};

#endif /* MAIN_WINDOW_H */
