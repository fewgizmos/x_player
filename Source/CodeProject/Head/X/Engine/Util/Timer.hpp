#pragma once

namespace Engine::Util::Timer
{

void NextFrame();

float MakeTargetFrameMs();

inline double Timestamp = 0;

inline float Delta   = 0;
inline float DeltaMs = 0;
inline float FPS     = 0;

}
