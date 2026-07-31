#ifndef INPUT_UTIL_H
#define INPUT_UTIL_H

#include <QObject>
#include <QRect>

class QJsonObject;

class InputUtil : public QObject
{
    Q_OBJECT
public:
    explicit InputUtil(QObject *parent = nullptr);
    static void execMouseEvent(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                               bool reliableMoveBoundary = false);
    static void execMouseEventOnScreen(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                       int screenIndex, bool reliableMoveBoundary = false);
    static void execMouseEventInRect(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                     const QRect &rect, bool reliableMoveBoundary = false);
    static void execMouseEventOnDesktopSource(int button, qreal x_n, qreal y_n, int mouseData, const QString &dwFlags,
                                              int desktopSourceIndex, bool reliableMoveBoundary = false);
    static void execKeyboardEvent(int keyCode,const QString& dwFlags);
    static void execKeyboardText(const QString &text);
    static bool execRemoteOperation(const QString &action, QString *errorMessage = nullptr);
    static bool execAndroidNavigation(const QString &action, QString *errorMessage = nullptr);
    static bool runProgram(const QString &path, QString *errorMessage = nullptr);
    static bool prepareWindowsInputBroker(QString *errorMessage = nullptr);
    static QString windowsInputBrokerServerName();
    static bool authenticateWindowsInputBrokerRequest(QJsonObject *request);
    static bool isWindowsUnattendedInputInstalled();
    static bool isWindowsUnattendedInputUpdateRequired(QString *reason = nullptr);
    static bool ensureWindowsUnattendedInputServiceReady(QString *errorMessage = nullptr);
    static bool installWindowsUnattendedInput(QString *errorMessage = nullptr);
    static bool uninstallWindowsUnattendedInput(QString *errorMessage = nullptr);
    static bool stopWindowsUnattendedInputService(QString *errorMessage = nullptr);
    static void setWindowsSessionLocked(bool locked);
    static bool isWindowsSessionLocked();
    static void shutdownWindowsInputBroker();
    static void cancelWindowsUnattendedInputReadiness();
    static void resetWindowsUnattendedInputReadinessCancellation();
    static int runWindowsInputBroker(int argc, char *argv[]);
    static int runWindowsInputBrokerService(int argc, char *argv[]);
    static int runWindowsServiceElevatedCommand(int argc, char *argv[]);
    static bool sendSecureAttentionSequence(QString *errorMessage = nullptr);
signals:
};

#endif /* INPUT_UTIL_H */
