#ifndef AIRAN_MOUSE_INPUT_POLICY_H
#define AIRAN_MOUSE_INPUT_POLICY_H

#include <algorithm>
#include <cstdint>
#include <optional>

class MouseMoveBurstPolicy
{
public:
    struct Dispatch
    {
        std::uint64_t sequence = 0;
        bool dispatchNow = false;
        bool reliableBoundary = false;
        int throttleDelayMs = 0;
        int tailDelayMs = 0;
    };

    explicit MouseMoveBurstPolicy(int minimumIntervalMs = 8, int tailDelayMs = 12)
        : m_minimumIntervalMs(std::max(1, minimumIntervalMs)),
          m_tailDelayMs(std::max(m_minimumIntervalMs, tailDelayMs))
    {
    }

    Dispatch observeMove(std::int64_t nowMs)
    {
        const std::uint64_t sequence = ++m_latestSequence;
        Dispatch dispatch;
        dispatch.sequence = sequence;
        dispatch.tailDelayMs = m_tailDelayMs;

        if (!m_burstActive)
        {
            m_burstActive = true;
            m_lastDispatchMs = nowMs;
            m_lastDispatchedSequence = sequence;
            m_lastBoundarySequence = sequence;
            dispatch.dispatchNow = true;
            dispatch.reliableBoundary = true;
            return dispatch;
        }

        const std::int64_t elapsed = std::max<std::int64_t>(0, nowMs - m_lastDispatchMs);
        if (elapsed >= m_minimumIntervalMs)
        {
            m_lastDispatchMs = nowMs;
            m_lastDispatchedSequence = sequence;
            dispatch.dispatchNow = true;
            return dispatch;
        }

        dispatch.throttleDelayMs = static_cast<int>(m_minimumIntervalMs - elapsed);
        return dispatch;
    }

    std::optional<Dispatch> takeThrottledMove(std::int64_t nowMs)
    {
        if (!m_burstActive || m_lastDispatchedSequence == m_latestSequence)
            return std::nullopt;

        m_lastDispatchMs = nowMs;
        m_lastDispatchedSequence = m_latestSequence;
        Dispatch dispatch;
        dispatch.sequence = m_latestSequence;
        dispatch.dispatchNow = true;
        return dispatch;
    }

    std::optional<Dispatch> finishBurst(std::int64_t nowMs)
    {
        if (!m_burstActive)
            return std::nullopt;

        m_burstActive = false;
        m_lastDispatchMs = nowMs;
        m_lastDispatchedSequence = m_latestSequence;
        if (m_lastBoundarySequence == m_latestSequence)
            return std::nullopt;

        m_lastBoundarySequence = m_latestSequence;
        Dispatch dispatch;
        dispatch.sequence = m_latestSequence;
        dispatch.dispatchNow = true;
        dispatch.reliableBoundary = true;
        return dispatch;
    }

    void markBoundaryFailed(std::uint64_t sequence)
    {
        if (sequence != 0 && m_lastBoundarySequence == sequence)
            --m_lastBoundarySequence;
    }

    std::optional<Dispatch> forceLatestBoundary() const
    {
        if (m_latestSequence == 0)
            return std::nullopt;
        Dispatch dispatch;
        dispatch.sequence = m_latestSequence;
        dispatch.dispatchNow = true;
        dispatch.reliableBoundary = true;
        return dispatch;
    }

    void reset()
    {
        m_burstActive = false;
        m_lastDispatchMs = 0;
        m_latestSequence = 0;
        m_lastDispatchedSequence = 0;
        m_lastBoundarySequence = 0;
    }

private:
    int m_minimumIntervalMs = 8;
    int m_tailDelayMs = 12;
    bool m_burstActive = false;
    std::int64_t m_lastDispatchMs = 0;
    std::uint64_t m_latestSequence = 0;
    std::uint64_t m_lastDispatchedSequence = 0;
    std::uint64_t m_lastBoundarySequence = 0;
};

#endif
