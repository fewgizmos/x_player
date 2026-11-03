#pragma once

namespace Engine::Base::Runtime::EngineCore
{

enum EngineThreadTag : unsigned char
{
    GameTh,
    RenderTh,
    ResourceLoaderTh,
};

inline bool WantsStop = false;

int Run();

void StartUp();
void CleanUp();

void TickFrame();

}
