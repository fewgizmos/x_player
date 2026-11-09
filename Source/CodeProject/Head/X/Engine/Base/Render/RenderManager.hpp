#pragma once

#include <vkb/VkBootstrap.h>

namespace Engine::Base::Render::It
{

void Init();
void Cleanup();
void RenderFrame();

void WaitIdle();

const vkb::Instance& GetInstance();

}
