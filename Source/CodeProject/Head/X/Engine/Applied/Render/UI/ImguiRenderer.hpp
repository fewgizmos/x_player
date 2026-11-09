#pragma once

namespace Engine::Applied::Render::UI::ImguiRenderer
{

void Init();
void Cleanup();

void BeginFrame();
void EndFrame();
void Render();

}
