#ifndef CONTROL_WINDOW_CONNECTION_PROGRESS_STATE_H
#define CONTROL_WINDOW_CONNECTION_PROGRESS_STATE_H

#include <QList>

namespace ControlConnectionProgress
{
constexpr int kConnectionStepCount = 6;

enum ConnectionStep
{
    StepLocalDecoder = 0,
    StepConnectRequest,
    StepRemoteEncoder,
    StepCodecNegotiation,
    StepConnectionResult,
    StepWaitingFrame
};

enum ConnectionStepState
{
    StepPending = 0,
    StepRunning,
    StepDone,
    StepFailed
};

/*
 * Ensures the state list matches the fixed connection step count.
 */
inline void ensureConnectionSteps(QList<int> &states)
{
    while (states.size() < kConnectionStepCount)
        states.append(StepPending);
    while (states.size() > kConnectionStepCount)
        states.removeLast();
}

/*
 * Marks non-failed steps before the target step as complete.
 */
inline void markPreviousStepsDone(QList<int> &states, int step)
{
    ensureConnectionSteps(states);
    for (int i = 0; i < step && i < states.size(); ++i)
    {
        if (states[i] != StepFailed)
            states[i] = StepDone;
    }
}

/*
 * Marks one step as running.
 */
inline void markStepRunning(QList<int> &states, int step)
{
    ensureConnectionSteps(states);
    markPreviousStepsDone(states, step);
    if (step >= 0 && step < states.size() && states[step] != StepDone && states[step] != StepFailed)
        states[step] = StepRunning;
}

/*
 * Marks one step as complete.
 */
inline void markStepDone(QList<int> &states, int step)
{
    ensureConnectionSteps(states);
    markPreviousStepsDone(states, step);
    if (step >= 0 && step < states.size())
        states[step] = StepDone;
}

/*
 * Marks one step as failed.
 */
inline void markStepFailed(QList<int> &states, int step)
{
    ensureConnectionSteps(states);
    markPreviousStepsDone(states, step);
    if (step >= 0 && step < states.size())
        states[step] = StepFailed;
}
} // namespace ControlConnectionProgress

#endif /* CONTROL_WINDOW_CONNECTION_PROGRESS_STATE_H */
