#pragma once

#include "X/Project/Configs/PreCompiled/Dynamic.hpp"

#include "X/Engine/Util/Applied/ResourceManager.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

namespace Engine::Applied::Render::Tests::ModelRenderer
{

void Init();
void Cleanup();
void Render();
::Engine::Util::Applied::ResourceState GetModelState();

}

#endif
