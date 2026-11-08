#pragma once

#include <functional>
#include <string>

namespace Engine::Base::Render::RenderingPool
{

using RenderCallback = std::function<void()>;

void Register(std::string p_Name, RenderCallback p_Callback, int p_Order = 50);
void Seal();
void RenderAll();
void Clear();

}
