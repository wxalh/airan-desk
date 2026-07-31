#include "terminal_session.h"
#include "common/logger_manager.h"

#include <QDir>
#include <QThread>

#if defined(Q_OS_WIN)

bool TerminalSession::startWindowsFallbackProcess()
{
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &m_inputWrite, &sa, 0) ||
        !CreatePipe(&m_outputRead, &outputWrite, &sa, 0))
    {
        if (inputRead)
            CloseHandle(inputRead);
        if (outputWrite)
            CloseHandle(outputWrite);
        closeConPty();
        emit errorOccurred(QStringLiteral("Failed to create Windows fallback terminal pipes"));
        return false;
    }

    SetHandleInformation(m_inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_outputRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(STARTUPINFOW);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inputRead;
    si.hStdOutput = outputWrite;
    si.hStdError = outputWrite;

    QString shell = QDir::toNativeSeparators(defaultShell());
    std::wstring application = shell.toStdWString();
    std::wstring command = L"\"" + application + L"\" ";
    command.append(windowsFallbackShellNativeArguments().toStdWString());

    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(application.c_str(), &command[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    if (!ok)
    {
        closeConPty();
        emit errorOccurred(QStringLiteral("Failed to start Windows fallback terminal shell"));
        return false;
    }

    CloseHandle(pi.hThread);
    m_processHandle = pi.hProcess;
    m_readerRunning.store(true);
    m_readerThread = std::thread(&TerminalSession::readerLoop, this);
    return true;
}

bool TerminalSession::startPty(int cols, int rows)
{
    typedef HRESULT(WINAPI * CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
    typedef void(WINAPI * ClosePseudoConsoleFn)(HPCON);
    auto kernel = GetModuleHandleW(L"kernel32.dll");
    auto createPseudoConsole = reinterpret_cast<CreatePseudoConsoleFn>(GetProcAddress(kernel, "CreatePseudoConsole"));
    auto closePseudoConsole = reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(kernel, "ClosePseudoConsole"));
    if (!createPseudoConsole || !closePseudoConsole)
    {
        LOG_WARN("ConPTY is not available; falling back to QProcess terminal");
        return false;
    }

    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &m_inputWrite, &sa, 0) ||
        !CreatePipe(&m_outputRead, &outputWrite, &sa, 0))
    {
        if (inputRead)
            CloseHandle(inputRead);
        if (outputWrite)
            CloseHandle(outputWrite);
        closeConPty();
        emit errorOccurred(QStringLiteral("Failed to create terminal pipes"));
        return false;
    }

    SetHandleInformation(m_inputWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(m_outputRead, HANDLE_FLAG_INHERIT, 0);

    COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    HRESULT hr = createPseudoConsole(size, inputRead, outputWrite, 0, &m_hpc);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr))
    {
        emit errorOccurred(QStringLiteral("Failed to create Windows pseudo console"));
        closeConPty();
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(STARTUPINFOEXW);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    si.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attrSize));
    if (!si.lpAttributeList || !InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attrSize))
    {
        if (si.lpAttributeList)
            HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        emit errorOccurred(QStringLiteral("Failed to initialize pseudo console attributes"));
        closeConPty();
        return false;
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   m_hpc, sizeof(HPCON), nullptr, nullptr))
    {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
        emit errorOccurred(QStringLiteral("Failed to attach pseudo console"));
        closeConPty();
        return false;
    }

    QString shell = QDir::toNativeSeparators(defaultShell());
    std::wstring application = shell.toStdWString();
    std::wstring command = L"\"" + application + L"\" ";
    command.append(windowsConPtyShellNativeArguments().toStdWString());
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(application.c_str(), &command[0], nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
                             &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(si.lpAttributeList);
    HeapFree(GetProcessHeap(), 0, si.lpAttributeList);

    if (!ok)
    {
        emit errorOccurred(QStringLiteral("Failed to start terminal shell"));
        closeConPty();
        return false;
    }

    CloseHandle(pi.hThread);
    m_processHandle = pi.hProcess;
    m_readerRunning.store(true);
    m_readerThread = std::thread(&TerminalSession::readerLoop, this);
    return true;
}

void TerminalSession::readerLoop()
{
    QByteArray buffer;
    buffer.resize(8192);
    while (m_readerRunning.load() && m_outputRead)
    {
        while (m_readerRunning.load() && !m_stopping.load() && m_outputPaused.load())
            QThread::msleep(10);
        if (!m_readerRunning.load() || m_stopping.load())
            break;
        DWORD readBytes = 0;
        BOOL ok = ReadFile(m_outputRead, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr);
        if (!ok || readBytes == 0)
            break;
        const QByteArray data = buffer.left(static_cast<int>(readBytes));
        const QString source = m_usingFallbackProcess ? QStringLiteral("pipe") : QStringLiteral("conpty");
        logWindowsTerminalBytes(data, source);
        emit outputReady(decodeWindowsOutput(data.constData(), data.size()));
    }

    if (!m_readerRunning.load())
        return;

    int exitCode = 0;
    if (m_processHandle)
    {
        DWORD code = 0;
        if (GetExitCodeProcess(m_processHandle, &code) && code != STILL_ACTIVE)
            exitCode = static_cast<int>(code);
    }
    emitClosedOnce(exitCode);
}

void TerminalSession::closeConPty()
{
    typedef void(WINAPI * ClosePseudoConsoleFn)(HPCON);
    static ClosePseudoConsoleFn closePseudoConsole =
        reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole"));

    m_stopping.store(true);
    m_readerRunning.store(false);
    if (m_inputWrite)
    {
        CloseHandle(m_inputWrite);
        m_inputWrite = nullptr;
    }
    cancelReaderIo();
    if (m_outputRead)
    {
        CloseHandle(m_outputRead);
        m_outputRead = nullptr;
    }
    if (m_processHandle)
    {
        WaitForSingleObject(m_processHandle, 1000);
        DWORD exitCode = STILL_ACTIVE;
        if (!GetExitCodeProcess(m_processHandle, &exitCode) || exitCode == STILL_ACTIVE)
        {
            TerminateProcess(m_processHandle, 0);
            WaitForSingleObject(m_processHandle, 1000);
        }
    }
    if (m_hpc && closePseudoConsole)
    {
        closePseudoConsole(m_hpc);
        m_hpc = nullptr;
    }
    if (m_readerThread.joinable())
        m_readerThread.join();
    if (m_outputRead)
    {
        CloseHandle(m_outputRead);
        m_outputRead = nullptr;
    }
    if (m_processHandle)
    {
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
}

void TerminalSession::cancelReaderIo()
{
    typedef BOOL(WINAPI * CancelSynchronousIoFn)(HANDLE);
    static CancelSynchronousIoFn cancelSynchronousIo =
        reinterpret_cast<CancelSynchronousIoFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelSynchronousIo"));

    if (!m_readerThread.joinable())
        return;

    if (!cancelSynchronousIo)
        return;

    HANDLE readerHandle = static_cast<HANDLE>(m_readerThread.native_handle());
    if (!readerHandle)
        return;

    if (!cancelSynchronousIo(readerHandle))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND)
        {
            LOG_WARN("Failed to cancel terminal reader IO: {}", static_cast<int>(error));
        }
    }
}

#endif


