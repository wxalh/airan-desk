#include "logger_manager.h"
#include "util/config/config_util.h"
#include "util/config/config_util_paths.h"
#include <QDir>
#include <QDateTime>
#include <QByteArray>
#include <QString>

namespace
{
constexpr std::size_t kMaxLogFileBytes = 100ULL * 1024ULL * 1024ULL;

QString defaultLogDirectory()
{
    const QString envLogDir = QString::fromLocal8Bit(qgetenv("AIRAN_DESK_LOG_DIR"));
    if (!envLogDir.isEmpty())
    {
        return QDir::cleanPath(envLogDir);
    }

    return config_util_internal::writableLogDir();
}
}

LoggerManager &LoggerManager::instance()
{
    static LoggerManager instance;
    return instance;
}

std::string log_arg_cast(const QString &arg)
{
    return arg.toStdString();
}

std::string log_arg_cast(QString &arg)
{
    return arg.toStdString();
}

std::string log_arg_cast(QString &&arg)
{
    return arg.toStdString();
}

std::string log_arg_cast(const QByteArray &arg)
{
    return QString::fromLocal8Bit(arg).toStdString();
}

std::string log_arg_cast(QByteArray &arg)
{
    return QString::fromLocal8Bit(arg).toStdString();
}

std::string log_arg_cast(QByteArray &&arg)
{
    return QString::fromLocal8Bit(arg).toStdString();
}

void LoggerManager::setLogLevel(std::shared_ptr<spdlog::logger> logger) const
{
    if (!logger)
        return;
    logger->set_level(ConfigUtil->logLevel);
}

void LoggerManager::setLogLevel(std::shared_ptr<spdlog::sinks::sink> sink) const
{
    if (!sink)
        return;
    sink->set_level(ConfigUtil->logLevel);
}

void LoggerManager::initialize(const QString &logFilePath)
{
    initialize(logFilePath.toStdString());
}

void LoggerManager::initialize(const std::string &logFilePath)
{
    if (m_initialized)
    {
        return;
    }

    try
    {
        if (auto existingLogger = spdlog::get("default"))
        {
            m_logger = existingLogger;
            setLogLevel(m_logger);
            m_initialized = true;
            m_logger->info("Logger attached to existing default logger");
            return;
        }

        
        QString logFileBase = QString::fromStdString(logFilePath);
        if (logFileBase.isEmpty())
        {
            logFileBase = defaultLogDirectory();
        }
        QDir().mkpath(logFileBase);
        logFileBase += "/airan-desk";
        
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        setLogLevel(console_sink);
        
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P/%t] [%^%l%$]\t[%n] - %v");

        
        file_sink = std::make_shared<spdlog::sinks::date_size_file_sink_mt>(
            logFileBase.toStdString(), kMaxLogFileBytes);
        setLogLevel(file_sink);
        
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%P/%t] [%l]\t[%n] - %v");

        
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        m_logger = std::make_shared<spdlog::logger>("default", sinks.begin(), sinks.end());

        
        setLogLevel(m_logger);
        m_logger->flush_on(spdlog::level::info);

        
        spdlog::register_logger(m_logger);
        spdlog::set_default_logger(m_logger);

        m_initialized = true;
        m_logger->info("Logger initialized, daily log files with a {} MiB size limit will be created in: {}",
                       kMaxLogFileBytes / (1024ULL * 1024ULL),
                       logFileBase.toStdString());
    }
    catch (const spdlog::spdlog_ex &ex)
    {
        
        m_logger = spdlog::get("log_fallback");
        if (!m_logger)
            m_logger = spdlog::stdout_color_mt("log_fallback");
        m_logger->error("Log initialization failed: {}", ex.what());
        m_initialized = true;
    }
}

std::shared_ptr<spdlog::logger> LoggerManager::getLogger(const QString &name)
{
    return getLogger(name.toStdString());
}

std::shared_ptr<spdlog::logger> LoggerManager::getLogger(const std::string &name)
{
    if (!m_initialized)
    {
        initialize();
    }
    std::string funcName = name;
    const std::size_t pos = funcName.find("::<lambda");
    if (pos != std::string::npos)
        funcName.resize(pos);

    if (funcName == "default" || funcName.empty())
    {
        return m_logger;
    }

    
    auto logger = spdlog::get(funcName);
    if (!logger)
    {
        
        try
        {
            
            auto sinks = m_logger->sinks();

            
            logger = std::make_shared<spdlog::logger>(funcName, sinks.begin(), sinks.end());

            
            setLogLevel(logger);

            
            logger->flush_on(spdlog::level::info);

            
            spdlog::register_logger(logger);
        }
        catch (const spdlog::spdlog_ex &ex)
        {
            
            m_logger->error("Failed to create logger '{}': {}", funcName, ex.what());
            logger = m_logger;
        }
    }
    setLogLevel(logger);
    return logger;
}
