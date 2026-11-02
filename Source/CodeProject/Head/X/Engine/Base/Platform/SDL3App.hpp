#pragma once

#include <SDL3/SDL_events.h>

#include <vector>

namespace Engine::Base::Platform::SDL3App
{

inline bool WantsExit = false;

void Init();
void Cleanup();

void PumpEvents();

const std::vector<SDL_Event>& GetEvents();

}
