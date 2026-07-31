#include "rtc/core/rtc_internal.h"

#include "media/codec/airan_video_codec_backend.h"
#include "common/logger_manager.h"

#include <rtc_base/logging.h>
#include <rtc_base/ssl_adapter.h>

#include <QString>

#include <mutex>

namespace rtc
{

std::atomic<int> g_instanceCount{0};

namespace
{

class NativeLogSink final : public LogSink
{
public:
    void OnLogMessage(const std::string &message) override { logLine(message); }
    void OnLogMessage(absl::string_view message) override { logLine(std::string(message)); }

private:
    
    static void logLine(const std::string &text)
    {
        auto logger = LoggerManager::instance().getLogger(QStringLiteral("webrtc"));
        size_t pos = 0;
        while (pos < text.size())
        {
            const size_t end = text.find_first_of("\r\n", pos);
            const size_t lineEnd = end == std::string::npos ? text.size() : end;
            if (lineEnd > pos)
            {
                const std::string line = text.substr(pos, lineEnd - pos);
                logger->debug("{}", line);
            }
            if (end == std::string::npos)
                break;
            pos = end + 1;
            while (pos < text.size() && (text[pos] == '\r' || text[pos] == '\n'))
                ++pos;
        }
    }
};

NativeLogSink g_nativeLogSink;
} // namespace


void ensureInitialized()
{
    static std::once_flag initFlag;
    std::call_once(initFlag, []() {
        InitializeSSL();
        LogMessage::SetLogToStderr(false);
        LogMessage::AddLogToStream(&g_nativeLogSink, LoggingSeverity::LS_INFO);
        airan::media::warmLocalVideoCodecCapabilities();
        LOG_INFO("Google WebRTC native runtime initialized");
    });
}

} // namespace rtc
