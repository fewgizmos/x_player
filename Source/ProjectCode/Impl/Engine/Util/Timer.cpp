#include "XProject/Engine/Util/Timer.hpp"

#include <algorithm>
#include <thread>
#include <chrono>

namespace Engine::Util::Timer
{

namespace
{

std::chrono::high_resolution_clock m_Timer{};

std::chrono::high_resolution_clock::time_point m_LastFrameEnd = m_Timer.now();
std::chrono::high_resolution_clock::time_point m_FrameStart   = m_Timer.now();

using ms = std::chrono::duration<float, std::milli>;

constexpr int   MAX_FPS   = 80;
constexpr float MAX_DELTA = 0.1f;

constexpr float MIN_FRAME_MS = 1000.0f / MAX_FPS;

}

void NextFrame()
{
    m_FrameStart = m_Timer.now();
    DeltaMs = std::chrono::duration_cast<ms>(m_FrameStart - m_LastFrameEnd).count();

    if (DeltaMs < MIN_FRAME_MS) {
        std::this_thread::sleep_for(ms(MIN_FRAME_MS - DeltaMs));
        m_FrameStart = m_Timer.now();
        DeltaMs = std::chrono::duration_cast<ms>(m_FrameStart - m_LastFrameEnd).count();
    }

    m_LastFrameEnd = m_FrameStart;

    FPS = 1000.0f / DeltaMs;

    Delta = std::min(DeltaMs / 1000.0f, MAX_DELTA);
    DeltaMs = Delta * 1000.0f;

    Timestamp += Delta;
}

float MakeTargetFrameMs()
{
    return MIN_FRAME_MS;
}

}
