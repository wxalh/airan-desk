#include "ui/control/control_window.h"
#include "ui/control/connection/control_window_connection_progress_state.h"
#include "ui/control/connection/control_window_connection_progress_view.h"

using namespace ControlConnectionProgress;

void ControlWindow::resetConnectionProgress()
{
    m_connectionStepStates.clear();
    ensureConnectionSteps(m_connectionStepStates);
    m_connectionStepStates[StepLocalDecoder] = StepRunning;
    m_connectionFailureReason.clear();
    renderConnectionProgress();
}

void ControlWindow::renderConnectionProgress()
{
    if (isReceivedImg || isClosing())
        return;

    ensureConnectionSteps(m_connectionStepStates);
    if (!m_connectionProgressView)
        m_connectionProgressView = new View(scrollArea.viewport());
    m_connectionProgressView->setProgress(m_connectionStepStates, m_connectionFailureReason);
}

void ControlWindow::markConnectionWaitingFrameDone()
{
    if (isReceivedImg || isClosing())
        return;
    isReceivedImg = true;
    markStepDone(m_connectionStepStates, StepWaitingFrame);
    if (m_connectionProgressView)
        m_connectionProgressView->complete();
    updateAndroidSidePanelWidth();
    m_toolbarAutoHidden = false;
    updateToolbarPosition();
    if (m_floatingToolbar)
    {
        m_toolbarShownPosition = m_floatingToolbar->pos();
        m_floatingToolbar->show();
        m_floatingToolbar->raise();
        m_toolbarAutoHideTimer->start(3000);
    }
}
