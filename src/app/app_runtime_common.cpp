#include "app/app_runtime_internal.h"

#include "common/logger_manager.h"
#include "rtc/core/rtc.hpp"

#include <QAbstractSocket>
#include <QDir>
#include <QJsonObject>

#include <memory>

#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace AppRuntime
{
    void registerCustomTypes()
    {
        qRegisterMetaType<QAbstractSocket::SocketState>("QAbstractSocket::SocketState");
        qRegisterMetaType<rtc::PeerConnection::GatheringState>("rtc::PeerConnection::GatheringState");
        qRegisterMetaType<rtc::PeerConnection::State>("rtc::PeerConnection::State");
        qRegisterMetaType<rtc::PeerConnection::IceState>("rtc::PeerConnection::IceState");
        qRegisterMetaType<rtc::message_variant>("rtc::message_variant");
        qRegisterMetaType<rtc::binary>("rtc::binary");
        qRegisterMetaType<std::shared_ptr<rtc::binary>>("std::shared_ptr<rtc::binary>");
#if defined(_WIN32) && defined(AIRAN_WEBRTC_WINDOWS_PLATFORM_WIN10)
        qRegisterMetaType<rtc::D3D11VideoFrame>("rtc::D3D11VideoFrame");
#endif
        qRegisterMetaType<QJsonObject>("QJsonObject");
    }

    void initLog()
    {
        LoggerManager::instance().initialize();
        LOG_INFO("The log service was successfully initialized with spdlog.");
    }

    bool isRunning(bool serviceMode)
    {
#if defined(Q_OS_WIN64) || defined(Q_OS_WIN32)
        static HANDLE hMainMutex = nullptr;
        static HANDLE hServiceMutex = nullptr;
        HANDLE &hMutex = serviceMode ? hServiceMutex : hMainMutex;
        if (hMutex)
            return false;

        hMutex = CreateMutexW(NULL,
                              TRUE,
                              serviceMode ? L"Global\\airan_service_mutex" : L"Global\\airan_mutex");
        if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS)
        {
            CloseHandle(hMutex);
            hMutex = NULL;
            return true;
        }
        return false;
#else
        static int mainLockFile = -1;
        static int serviceLockFile = -1;
        int &lockFile = serviceMode ? serviceLockFile : mainLockFile;
        if (lockFile == -1)
        {
            QString lockName = serviceMode
                                   ? QStringLiteral("airan-service-%1.lock")
                                   : QStringLiteral("airan-%1.lock");
            QString lockPath = QDir::temp().absoluteFilePath(
                lockName.arg(static_cast<qulonglong>(getuid())));
            lockFile = open(lockPath.toLocal8Bit().constData(), O_RDWR | O_CREAT, 0644);
        }

        return lockFile != -1 && flock(lockFile, LOCK_EX | LOCK_NB) == -1;
#endif
    }
}
