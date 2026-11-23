#pragma once

#include "X/Engine/Base/Render/RenderingPool.hpp"

namespace Engine::Applied::Render::SceneRendering::It
{

void Init();
void Cleanup();

void Render(const ::Engine::Base::Render::RenderingPool::RenderTarget& p_Target);

}
