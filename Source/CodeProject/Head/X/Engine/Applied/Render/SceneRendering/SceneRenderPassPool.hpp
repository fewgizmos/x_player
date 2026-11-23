#pragma once

#include <functional>
#include <string>

namespace Engine::Applied::Render::SceneRendering::SceneRenderPassPool
{

using RenderPass = std::function<void()>;

void Register(std::string p_Name, RenderPass p_Callback, int p_Order);

void Seal();

void RenderAll();

void Clear();

}
