#ifndef LOGGER_MANAGER_H
#define LOGGER_MANAGER_H

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include "common/date_size_file_sink.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class QByteArray;
class QString;

/* MSVC:  "int __cdecl ns::Class::func(int,char **)"  -> "ns::Class::func" */
/* GCC:   "int ns::Class::func(int, char**)"           -> "ns::Class::func" */
inline std::string _extract_func_name(const char *sig)
{
    std::string s = sig ? sig : "";
    const std::size_t paren = s.find('(');
    if (paren != std::string::npos)
        s.resize(paren);
    const auto eraseAll = [&s](const char *token) {
        const std::string needle(token);
        for (std::size_t pos = s.find(needle); pos != std::string::npos; pos = s.find(needle))
            s.erase(pos, needle.size());
    };
    eraseAll("__cdecl ");
    eraseAll("__thiscall ");
    eraseAll("auto ");
    const std::size_t lastSpace = s.find_last_of(' ');
    if (lastSpace != std::string::npos)
    {
        s.erase(0, lastSpace + 1);
    }
    return s;
}

#if defined(_MSC_VER)
#define CLASS_AND_FUNCTION() _extract_func_name(__FUNCSIG__)
#elif defined(__GNUC__) || defined(__clang__)
#define CLASS_AND_FUNCTION() _extract_func_name(__PRETTY_FUNCTION__)
#else
#define CLASS_AND_FUNCTION() _extract_func_name(__func__)
#endif

template <typename T>
inline auto log_arg_cast(T &&arg) -> decltype(std::forward<T>(arg))
{
    return std::forward<T>(arg);
}
std::string log_arg_cast(const QString &arg);
std::string log_arg_cast(QString &arg);
std::string log_arg_cast(QString &&arg);
std::string log_arg_cast(const QByteArray &arg);
std::string log_arg_cast(QByteArray &arg);
std::string log_arg_cast(QByteArray &&arg);

template <typename... Args, std::size_t... I>
auto log_cast_tuple_impl(const std::tuple<Args...> &t, std::index_sequence<I...>)
{
    return std::make_tuple(log_arg_cast(std::get<I>(t))...);
}
template <typename... Args>
auto log_cast_tuple(Args &&...args)
{
    auto t = std::forward_as_tuple(std::forward<Args>(args)...);
    return log_cast_tuple_impl(t, std::index_sequence_for<Args...>{});
}

template <typename Tuple, typename F, std::size_t... I>
void log_apply(Tuple &&t, F &&f, std::index_sequence<I...>)
{
    f(std::get<I>(std::forward<Tuple>(t))...);
}
template <typename Tuple, typename F>
void log_apply(Tuple &&t, F &&f)
{
    constexpr auto size = std::tuple_size<std::decay_t<Tuple>>::value;
    log_apply(std::forward<Tuple>(t), std::forward<F>(f), std::make_index_sequence<size>{});
}

enum class LogSeverity
{
    Trace,
    Debug,
    Info,
    Warn,
    Error
};

template <LogSeverity Severity, typename LoggerPtr, typename Format>
class LogInvoker
{
public:
    LogInvoker(LoggerPtr logger, Format format)
        : m_logger(std::move(logger)), m_format(std::move(format))
    {
    }

    template <typename... Args>
    void operator()(Args &&...args) const
    {
        if constexpr (Severity == LogSeverity::Trace)
        {
            m_logger->trace(SPDLOG_FMT_RUNTIME(m_format), std::forward<Args>(args)...);
        }
        else if constexpr (Severity == LogSeverity::Debug)
        {
            m_logger->debug(SPDLOG_FMT_RUNTIME(m_format), std::forward<Args>(args)...);
        }
        else if constexpr (Severity == LogSeverity::Info)
        {
            m_logger->info(SPDLOG_FMT_RUNTIME(m_format), std::forward<Args>(args)...);
        }
        else if constexpr (Severity == LogSeverity::Warn)
        {
            m_logger->warn(SPDLOG_FMT_RUNTIME(m_format), std::forward<Args>(args)...);
        }
        else if constexpr (Severity == LogSeverity::Error)
        {
            m_logger->error(SPDLOG_FMT_RUNTIME(m_format), std::forward<Args>(args)...);
        }
    }

private:
    LoggerPtr m_logger;
    Format m_format;
};

template <LogSeverity Severity, typename LoggerPtr, typename Format>
auto makeLogInvoker(LoggerPtr &&logger, Format &&format)
{
    return LogInvoker<Severity, std::decay_t<LoggerPtr>, std::decay_t<Format>>(
        std::forward<LoggerPtr>(logger),
        std::forward<Format>(format));
}

class LoggerManager
{
public:
    static LoggerManager &instance();

    void initialize(const std::string &logFilePath = "");
    void initialize(const QString &logFilePath);
    std::shared_ptr<spdlog::logger> getLogger(const std::string &name = "default");
    std::shared_ptr<spdlog::logger> getLogger(const QString &name);

private:
    LoggerManager() = default;
    ~LoggerManager() = default;
    LoggerManager(const LoggerManager &) = delete;
    LoggerManager &operator=(const LoggerManager &) = delete;

    void setLogLevel(std::shared_ptr<spdlog::logger> logger) const;
    void setLogLevel(std::shared_ptr<spdlog::sinks::sink> sink) const;

    std::shared_ptr<spdlog::logger> m_logger;
    bool m_initialized = false;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink;
    std::shared_ptr<spdlog::sinks::date_size_file_sink_mt> file_sink;
};

#define LOG_GENERIC(LOGGER_PTR, LEVEL, FMT, ...)                           \
    do                                                                     \
    {                                                                      \
        auto logger = (LOGGER_PTR);                                        \
        auto tuple = log_cast_tuple(__VA_ARGS__);                          \
        log_apply(tuple, makeLogInvoker<LogSeverity::LEVEL>(logger, FMT)); \
    } while (0)

#define LOG_TRACE(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Trace, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Warn, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Warn, fmt, ##__VA_ARGS__)
#define LOG_VERBOSE(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Trace, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LOG_GENERIC(LoggerManager::instance().getLogger(CLASS_AND_FUNCTION()), Error, fmt, ##__VA_ARGS__)
#endif /* LOGGER_MANAGER_H */
