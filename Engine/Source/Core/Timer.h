// Timer.h : high-resolution frame timing.
#pragma once

#include <Windows.h>

// GetTickCount64 only has ~15 ms resolution - useless when a frame takes
// 6 ms. QueryPerformanceCounter ticks millions of times per second.
class Timer
{
public:
    Timer()
    {
        QueryPerformanceFrequency(&m_frequency);
        QueryPerformanceCounter(&m_start);
        m_previous = m_start;
    }

    // Seconds since the previous call.
    float Tick()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const float dt = float(double(now.QuadPart - m_previous.QuadPart) /
                               double(m_frequency.QuadPart));
        m_previous = now;
        return dt;
    }

    float TotalSeconds() const
    {
        return float(double(m_previous.QuadPart - m_start.QuadPart) /
                     double(m_frequency.QuadPart));
    }

private:
    LARGE_INTEGER m_frequency = {};
    LARGE_INTEGER m_start     = {};
    LARGE_INTEGER m_previous  = {};
};
