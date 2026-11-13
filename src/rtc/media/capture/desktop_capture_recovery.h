#pragma once

#include "desktop_capture/desktop_capturer.h"

namespace rtc
{

// Recovery decisions are shared by the capture loop and result callback.
class DesktopCaptureRecovery
{
public:
    enum class FailureAction { Ignore, ReportFailure, Recreate, Stop };

    void reset(bool delegated)
    {
        m_delegated = delegated;
        m_stopped = false;
        m_consecutiveFailures = 0;
    }

    FailureAction onFailure(airan::desktop_capture::DesktopCapturer::Result result)
    {
        if (m_stopped)
            return FailureAction::Ignore;
        if (m_delegated)
        {
            // The portal returns temporary errors while the user selects a
            // source, and while an authorized stream has no new frame yet.
            // Recreating it would cancel and reopen the authorization dialog.
            if (result != airan::desktop_capture::DesktopCapturer::Result::ERROR_PERMANENT)
                return FailureAction::Ignore;

            // Cancellation or session closure requires user intervention.
            // Report it once; never reopen authorization automatically.
            m_stopped = true;
            return FailureAction::Stop;
        }
        if (++m_consecutiveFailures >= 10)
        {
            m_consecutiveFailures = 0;
            return FailureAction::Recreate;
        }
        return FailureAction::ReportFailure;
    }

    void onFrame() { m_consecutiveFailures = 0; }
    bool stopped() const { return m_stopped; }

private:
    bool m_delegated = false;
    bool m_stopped = false;
    int m_consecutiveFailures = 0;
};

} // namespace rtc
