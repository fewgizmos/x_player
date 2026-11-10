#include "X/Engine/Base/Platform/SDL3App.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include <SDL3/SDL_init.h>

namespace Engine::Base::Platform::SDL3App
{

namespace
{

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("sdl3app_init", Init, 100);

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("sdl3app_clean", Cleanup, 0);

const JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    Runtime::EngineCore::GameTh, "sdl3app_event_pump", PumpEvents, 90
);

}

std::vector<SDL_Event> m_EventsBuf;

void Init()
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
}

void Cleanup()
{
    m_EventsBuf.clear();
    WantsExit = false;

    SDL_Quit();
}

void PumpEvents()
{
    m_EventsBuf.clear();

    SDL_Event Event;

    while (SDL_PollEvent(&Event)) {
        if (Event.type == SDL_EVENT_QUIT) {
            WantsExit = true;
        }

        m_EventsBuf.push_back(Event);
    }
}

const std::vector<SDL_Event>& GetEvents() { return m_EventsBuf; }

}
