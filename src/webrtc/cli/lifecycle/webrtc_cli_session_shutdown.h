#ifndef WEBRTC_CLI_SESSION_SHUTDOWN_H
#define WEBRTC_CLI_SESSION_SHUTDOWN_H

class QThread;
class WebRtcCli;

namespace WebRtcCliSessionShutdown
{
bool shutdown(WebRtcCli *webrtcCli, QThread *rtcCliThread);
}

#endif /* WEBRTC_CLI_SESSION_SHUTDOWN_H */
