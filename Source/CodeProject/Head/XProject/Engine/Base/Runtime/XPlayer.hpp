#pragma once

namespace Engine::Base::Runtime::EngineCore
{

inline bool WantsStop = false;

int Run();

void StartUp();
void CleanUp();

void TickFrame();

}
