#pragma once

#include "X/Engine/Base/Render/RenderingPool.hpp"

namespace Engine::Applied::Render::UI::ImguiRenderer
{

void Init();
void Cleanup();

void BeginFrame();
void EndFrame();
void Render(const ::Engine::Base::Render::RenderingPool::RenderTarget& p_Target);

}
