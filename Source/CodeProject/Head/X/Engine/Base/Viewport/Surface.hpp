#pragma once

#include <SDL3/SDL_video.h>

#include <glm/vec2.hpp>

namespace Engine::Base::Viewport::Surface
{

void SetupForVulkan();
void Destroy();

SDL_Window* GetNative();
glm::ivec2 GetExtent();

bool GetWantsExit();

bool GetUnhandleResized();
void ClearUnhandleResized();

}
