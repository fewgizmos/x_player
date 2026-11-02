#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include "X/Engine/Util/Timer.hpp"

#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/Platform/SDL3WM.hpp"

#include "X/Engine/Base/Viewport/Inputs.hpp"
#include "X/Engine/Base/Viewport/Surface.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

namespace Engine::Base::Runtime::EngineCore
{

int Run()
{
    try {
        StartUp();

        while (!WantsStop) {
            TickFrame();
        }

        CleanUp();
    }

    catch (const std::exception& Error) {
        std::cerr << Error.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void StartUp()
{
    Platform::SDL3App::Init();

    Viewport::Surface::SetupForVulkan();
}

void CleanUp()
{
    Viewport::Surface::Destroy();

    Platform::SDL3WM::Cleanup();
    Platform::SDL3App::Cleanup();
}

void TickFrame()
{
    Util::Timer::NextFrame();

    Platform::SDL3App::PumpEvents();
    Platform::SDL3WM::PumpEventsToWindows();

    Viewport::Inputs::ParseFrameInputs();

    if (Platform::SDL3App::WantsExit || Viewport::Surface::GetWantsExit()) {
        WantsStop = true;
    }
}

}
