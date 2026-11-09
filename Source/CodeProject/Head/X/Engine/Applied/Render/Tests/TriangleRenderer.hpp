#pragma once

#include "X/Configs/PreCompiled/Dynamic.hpp"

#ifdef XP_DEV_FEATURES_ENABLED

namespace Engine::Applied::Render::Tests::TriangleRenderer
{

void Init();
void Cleanup();
void Render();

}

#endif
