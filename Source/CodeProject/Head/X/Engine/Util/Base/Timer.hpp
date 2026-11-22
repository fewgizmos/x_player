#pragma once

namespace Engine::Util::Base::Timer
{

void NextFrame();

float MakeTargetFrameMs();

inline double Timestamp = 0;

inline float Delta   = 0;
inline float DeltaMs = 0;
inline float FPS     = 0;

}
