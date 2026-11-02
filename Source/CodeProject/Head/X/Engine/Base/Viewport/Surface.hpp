#pragma once

#include "X/Engine/Base/Platform/SDL3WM.hpp"

#include <glm/vec2.hpp>

namespace Engine::Base::Viewport::Surface
{

inline Platform::SDL3WM::Window* m_Win = nullptr;

void SetupForVulkan();
void Destroy();

inline SDL_Window* GetNative() { return Platform::SDL3WM::GetNative(m_Win); }
inline glm::ivec2 GetExtent()  { return Platform::SDL3WM::GetExtent(m_Win); }

inline bool GetWantsExit() { return m_Win->WantsClose; }

inline bool GetUnhandleResized() { return m_Win->UnhandleResized; }
inline void ClearUnhandleResized() { m_Win->UnhandleResized = false; }

}
