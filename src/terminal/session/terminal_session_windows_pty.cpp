#include "terminal_session.h"
#include "common/logger_manager.h"

#include <QDir>
#include <QMetaObject>
#include <QThread>

#include <system_error>

#if defined(Q_OS_WIN)

bool TerminalSession::startWindowsFallbackProcess()
{
    // ConPTY failure cleanup marks the session as stopping; a fallback retry
    // is a new child session and must restart its reader state.
    m_stopping.store(false);
    m_readerRunning.store(false);
    m_usingFallbackProcess = false;
    SECURITY_ATTRIBUTES sa{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (!CreatePipe(&inputRead, &m_inputWrite, &sa, 0) ||
        !CreatePipe(&m_outputRead, &outputWrite, &sa, 0))
    {
        if (inputRead)
            CloseHandle(inputRead);
        if (m_inputWrite)
        {
            CloseHandle(m_inputWrite);
            m_inputWrite = nullptr;
        }
        if (m_outputRead)
        {
            CloseHandle(m_outputRead);
            m_outputRead = nullptr;
        }
        if (outputWrite)
            CloseHandle(outputWrite);
        closeConPty();
        return false;
    }

    if (!SetHandleInformation(m_inputWrite, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(m_outputRead, HANDLE_FLAG_INHERIT, 0))
    {
        const DWORD error = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(outputWrite);
        closeConPty();
        LOG_WARN("Failed to protect Windows fallback pipe handles from inheritance: error={}",
                 static_cast<int>(error));
        return false;
    }

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
        return false;
    }

    CloseHandle(pi.hThread);
    m_processHandle = pi.hProcess;
    m_usingFallbackProcess = true;
    m_readerRunning.store(true);
    const quint64 generation = ++m_windowsSessionGeneration;
    try
    {
        startWindowsInputWriter(generation);
        m_readerThread = std::thread(&TerminalSession::readerLoop, this, generation);
    }
    catch (const std::system_error &error)
    {
        LOG_ERROR("Failed to create Windows terminal fallback threads: generation={}, error={}",
                  generation,
                  error.what());
        closeConPty();
        return false;
    }
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
        if (m_inputWrite)
        {
            CloseHandle(m_inputWrite);
            m_inputWrite = nullptr;
        }
        if (m_outputRead)
        {
            CloseHandle(m_outputRead);
            m_outputRead = nullptr;
        }
        if (outputWrite)
            CloseHandle(outputWrite);
        closeConPty();
        return false;
    }

    if (!SetHandleInformation(m_inputWrite, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(m_outputRead, HANDLE_FLAG_INHERIT, 0))
    {
        const DWORD error = GetLastError();
        CloseHandle(inputRead);
        CloseHandle(outputWrite);
        closeConPty();
        LOG_WARN("Failed to protect ConPTY pipe handles from inheritance: error={}",
                 static_cast<int>(error));
        return false;
    }

    COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
    HRESULT hr = createPseudoConsole(size, inputRead, outputWrite, 0, &m_hpc);
    CloseHandle(inputRead);
    CloseHandle(outputWrite);
    if (FAILED(hr))
    {
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
        closeConPty();
        return false;
    }

    if (!UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                                   m_hpc, sizeof(HPCON), nullptr, nullptr))
    {
        DeleteProcThreadAttributeList(si.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, si.lpAttributeList);
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
        closeConPty();
        return false;
    }

    CloseHandle(pi.hThread);
    m_processHandle = pi.hProcess;
    m_readerRunning.store(true);
    const quint64 generation = ++m_windowsSessionGeneration;
    try
    {
        startWindowsInputWriter(generation);
        m_readerThread = std::thread(&TerminalSession::readerLoop, this, generation);
    }
    catch (const std::system_error &error)
    {
        LOG_ERROR("Failed to create Windows ConPTY threads: generation={}, error={}",
                  generation,
                  error.what());
        closeConPty();
        return false;
    }
    return true;
}

void TerminalSession::readerLoop(quint64 generation)
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
        const QByteArray decoded = m_usingFallbackProcess
                                       ? decodeWindowsConsoleBytes(data)
                                       : decodeWindowsOutput(data.constData(), data.size());
        emit outputReady(decoded);
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
    if (!QMetaObject::invokeMethod(this,
                                   "finalizeWindowsProcessExit",
                                   Qt::QueuedConnection,
                                   Q_ARG(quint64, generation),
                                   Q_ARG(int, exitCode)))
    {
        LOG_ERROR("Failed to queue cleanup for an exited Windows terminal process");
    }
}

void TerminalSession::finalizeWindowsProcessExit(quint64 generation, int exitCode)
{
    if (generation != m_windowsSessionGeneration || !m_processHandle)
        return;

    closeConPty();
    m_usingFallbackProcess = false;
    emitClosedOnce(exitCode);
}

void TerminalSession::closeConPty()
{
    typedef void(WINAPI * ClosePseudoConsoleFn)(HPCON);
    static ClosePseudoConsoleFn closePseudoConsole =
        reinterpret_cast<ClosePseudoConsoleFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole"));

    m_stopping.store(true);
    m_readerRunning.store(false);
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
    stopWindowsInputWriter();
    if (m_inputWrite)
    {
        CloseHandle(m_inputWrite);
        m_inputWrite = nullptr;
    }
    // Terminating the child first closes its inherited pipe end and lets the
    // reader leave ReadFile cleanly. Closing the member handle before joining
    // the reader races with ReadFile and can leave stop() blocked indefinitely.
    cancelReaderIo();
    if (m_readerThread.joinable())
        m_readerThread.join();
    if (m_outputRead)
        CloseHandle(m_outputRead);
    if (m_hpc && closePseudoConsole)
    {
        closePseudoConsole(m_hpc);
        m_hpc = nullptr;
    }
    m_outputRead = nullptr;
    if (m_processHandle)
    {
        CloseHandle(m_processHandle);
        m_processHandle = nullptr;
    }
}

void TerminalSession::stopWindowsInputWriter()
{
    m_writerRunning.store(false);
    {
        std::lock_guard<std::mutex> lock(m_writerMutex);
        m_writerPendingInput.clear();
    }
    m_writerCondition.notify_all();
    cancelWriterIo();
    if (m_writerThread.joinable())
        m_writerThread.join();
}

void TerminalSession::cancelWriterIo()
{
    typedef BOOL(WINAPI * CancelSynchronousIoFn)(HANDLE);
    static CancelSynchronousIoFn cancelSynchronousIo =
        reinterpret_cast<CancelSynchronousIoFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelSynchronousIo"));

    if (!cancelSynchronousIo || !m_writerThread.joinable())
        return;

    HANDLE writerHandle = static_cast<HANDLE>(m_writerThread.native_handle());
    if (!writerHandle || cancelSynchronousIo(writerHandle))
        return;

    const DWORD error = GetLastError();
    if (error != ERROR_NOT_FOUND)
        LOG_WARN("Failed to cancel terminal writer IO: {}", static_cast<int>(error));
}

void TerminalSession::cancelReaderIo()
{
    typedef BOOL(WINAPI * CancelIoExFn)(HANDLE, LPOVERLAPPED);
    typedef BOOL(WINAPI * CancelSynchronousIoFn)(HANDLE);
    static CancelIoExFn cancelIoEx =
        reinterpret_cast<CancelIoExFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelIoEx"));
    static CancelSynchronousIoFn cancelSynchronousIo =
        reinterpret_cast<CancelSynchronousIoFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "CancelSynchronousIo"));

    if (m_outputRead && cancelIoEx && !cancelIoEx(m_outputRead, nullptr))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND && error != ERROR_OPERATION_ABORTED)
            LOG_WARN("Failed to cancel terminal output IO: error={}", static_cast<int>(error));
    }

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


