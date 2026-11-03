#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include "X/Engine/Util/Timer.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"

#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/Platform/SDL3WM.hpp"

#include "X/Engine/Base/Viewport/Surface.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>

namespace Engine::Base::Runtime::EngineCore
{

namespace
{

void CheckExit()
{
    if (Platform::SDL3App::WantsExit || Viewport::Surface::GetWantsExit()) {
        WantsStop = true;
    }
}

}

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

    JobSystem::It::AttachCurrentThread(EngineThreadTag::GameTh);
    JobSystem::It::CreateThread(       EngineThreadTag::RenderTh);
    JobSystem::It::CreateThread(       EngineThreadTag::ResourceLoaderTh);

    JobSystem::DeferredTaskRegistration::Apply();

    JobSystem::It::AddTask(EngineThreadTag::GameTh, "checking_exit", CheckExit, 0);
}

void CleanUp()
{
    JobSystem::It::Shutdown();

    Viewport::Surface::Destroy();

    Platform::SDL3WM::Cleanup();
    Platform::SDL3App::Cleanup();
}

void TickFrame()
{
    Util::Timer::NextFrame();

    JobSystem::It::RunAll();
    JobSystem::It::WaitAll();
}

}
