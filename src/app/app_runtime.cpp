#include "app_runtime.h"

#include "app/app_runtime_internal.h"

#include "util/input/input_util.h"

#include <QApplication>
#include <QMetaObject>
#include <QStringList>

#include <cstdio>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <Windows.h>
#endif

namespace
{
    bool isVersionArgument(const QString &arg)
    {
        return arg == QStringLiteral("-v") ||
               arg == QStringLiteral("-V") ||
               arg == QStringLiteral("-version") ||
               arg == QStringLiteral("--version");
    }

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    SERVICE_STATUS_HANDLE g_serviceStatusHandle = nullptr;
    SERVICE_STATUS g_serviceStatus{};
    int g_serviceArgc = 0;
    char **g_serviceArgv = nullptr;

    
    void reportServiceStatus(DWORD currentState, DWORD win32ExitCode = NO_ERROR, DWORD waitHint = 0)
    {
        if (!g_serviceStatusHandle)
            return;

        g_serviceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
        g_serviceStatus.dwCurrentState = currentState;
        g_serviceStatus.dwWin32ExitCode = win32ExitCode;
        g_serviceStatus.dwWaitHint = waitHint;
        g_serviceStatus.dwControlsAccepted =
            currentState == SERVICE_RUNNING ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN) : 0;
        SetServiceStatus(g_serviceStatusHandle, &g_serviceStatus);
    }

    
    void WINAPI serviceCtrlHandler(DWORD control)
    {
        if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
        {
            reportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 3000);
            if (qApp)
                QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection);
        }
    }

    
    void WINAPI serviceMain(DWORD, LPWSTR *)
    {
        g_serviceStatusHandle = RegisterServiceCtrlHandlerW(L"airan-desk", serviceCtrlHandler);
        if (!g_serviceStatusHandle)
            return;

        reportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
        reportServiceStatus(SERVICE_RUNNING);
        QStringList args;
        for (int i = 0; i < g_serviceArgc; ++i)
            args << QString::fromLocal8Bit(g_serviceArgv[i]);
        const int result = args.contains(QStringLiteral("--windows-input-broker"))
                               ? InputUtil::runWindowsInputBrokerService(g_serviceArgc, g_serviceArgv)
                               : static_cast<int>(ERROR_INVALID_PARAMETER);
        reportServiceStatus(SERVICE_STOPPED, static_cast<DWORD>(result));
    }
#endif
}


int runAiranDesk(int argc, char *argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);

    for (int i = 1; i < args.size(); ++i)
    {
        if (isVersionArgument(args.at(i)))
        {
            std::printf("airan-desk %s\n", AIRAN_DESK_VERSION);
            return 0;
        }
    }

    const bool serviceMode = args.contains(QStringLiteral("--service"));
    const bool forceNoUi = serviceMode;

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
    if (args.contains(QStringLiteral("--airan-elevation-launcher")) ||
        args.contains(QStringLiteral("--airan-install-service-elevated")) ||
        args.contains(QStringLiteral("--airan-uninstall-service-elevated")) ||
        args.contains(QStringLiteral("--airan-start-service-elevated")))
        return InputUtil::runWindowsServiceElevatedCommand(argc, argv);

    if (serviceMode)
    {
        g_serviceArgc = argc;
        g_serviceArgv = argv;
        SERVICE_TABLE_ENTRYW dispatchTable[] = {
            {const_cast<LPWSTR>(L"airan-desk"), serviceMain},
            {nullptr, nullptr}};
        if (StartServiceCtrlDispatcherW(dispatchTable))
            return 0;
        if (GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
            return static_cast<int>(GetLastError());
    }

    if (args.contains(QStringLiteral("--windows-input-broker")))
        return InputUtil::runWindowsInputBroker(argc, argv);
#endif

    return AppRuntime::runApplication(argc, argv, forceNoUi);
}
