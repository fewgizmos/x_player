#pragma once

#include <functional>

namespace Engine::Base::Render::ArtifactsDeletor
{

void Push(std::function<void()> p_Callback);
void CleanAll();

}
