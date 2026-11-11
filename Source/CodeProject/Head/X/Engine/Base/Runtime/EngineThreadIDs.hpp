#pragma once

namespace Engine::Base::Runtime
{

enum EngineThreadID : unsigned char
{
    GameTh,
    RenderTh,
    ResourceLoaderTh,
};

}
