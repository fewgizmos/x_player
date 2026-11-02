#include "X/Engine/Util/Timer.hpp"

#include "X/Configs/Static.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

namespace Engine::Util::Timer
{

namespace
{

using Clock = std::chrono::steady_clock;
using ms    = std::chrono::duration<float, std::milli>;

Clock::time_point m_LastFrameEnd{};

bool m_IsStarted = false;

constexpr float MAX_DELTA = 0.1f;

constexpr float MIN_FRAME_MS = 1000.0f / Configs::Static::MAX_FPS;

constexpr std::chrono::microseconds SPIN_MARGIN{1000};

}

void NextFrame()
{
    if (!m_IsStarted) {
        m_LastFrameEnd = Clock::now();
        m_IsStarted    = true;
    }

    const Clock::time_point TargetTime = m_LastFrameEnd + std::chrono::duration_cast<Clock::duration>(ms(MIN_FRAME_MS));

    if (Clock::now() + SPIN_MARGIN < TargetTime) {
        std::this_thread::sleep_until(TargetTime - SPIN_MARGIN);
    }

    while (Clock::now() < TargetTime) {
        std::this_thread::yield();
    }

    const Clock::time_point FrameStart = Clock::now();

    DeltaMs = std::chrono::duration_cast<ms>(FrameStart - m_LastFrameEnd).count();

    m_LastFrameEnd = FrameStart;

    FPS = 1000.0f / DeltaMs;

    Delta   = std::min(DeltaMs / 1000.0f, MAX_DELTA);
    DeltaMs = Delta * 1000.0f;

    Timestamp += Delta;
}

float MakeTargetFrameMs()
{
    return MIN_FRAME_MS;
}

}
