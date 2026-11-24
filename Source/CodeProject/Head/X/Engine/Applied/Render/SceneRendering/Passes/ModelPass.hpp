#pragma once

#include "X/Engine/Base/Render/Vulkan/Shader.hpp"

namespace Engine::Applied::Render::SceneRendering::Passes::ModelPass
{

void Init();

void ConfigureGeometry(::Engine::Base::Render::GraphicsPipelineCreateInfo& p_Pipeline);
void ConfigureTransparency(::Engine::Base::Render::GraphicsPipelineCreateInfo& p_Pipeline);

}
