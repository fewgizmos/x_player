#pragma once

#include <functional>
#include <string>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

struct Frame;

}

namespace Engine::Applied::Render::SceneRendering::SceneRenderPassPool
{

enum class Phase
{
    Geometry,
    Light,
    Transparent
};

using RenderPass = std::function<void(const SceneList::Frame&)>;

void Register(std::string p_Name, Phase p_Phase, RenderPass p_Callback, int p_Order);

void Seal();

void RenderAll(Phase p_Phase, const SceneList::Frame& p_Frame);

void Clear();

}
