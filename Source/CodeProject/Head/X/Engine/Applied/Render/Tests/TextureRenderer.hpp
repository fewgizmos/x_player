#pragma once

#include "X/Project/Configs/PreCompiled/Dynamic.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

namespace Engine::Applied::Render::Tests::TextureRenderer
{

void Init();
void Cleanup();
void Render();

}

#endif
