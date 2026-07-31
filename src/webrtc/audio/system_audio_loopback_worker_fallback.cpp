#include "system_audio_loopback_worker.h"

#include "common/logger_manager.h"

#include <QtGlobal>

#if !defined(Q_OS_WIN) && !defined(Q_OS_LINUX) && !defined(Q_OS_MACOS)

void SystemAudioLoopbackWorker::captureSystemAudio()
{
    LOG_WARN("System audio loopback capture is not implemented on this platform");
}


void SystemAudioLoopbackWorker::captureMicrophoneAudio()
{
    LOG_WARN("Microphone capture is not implemented on this platform");
}
#endif
