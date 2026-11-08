#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include "X/Engine/Util/Timer.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"

#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/BootstrapManager/BootstrapManager.hpp"

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

const JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    GameTh, "checking_exit", CheckExit, 0
);

}

int Run()
{
    std::exception_ptr Failure;

    try {
        StartUp();

        while (!WantsStop) {
            TickFrame();
        }
    }
    catch (...) {
        Failure = std::current_exception();
    }

    try {
        CleanUp();
    }
    catch (...) {
        if (Failure) {
            try {
                throw;
            }
            catch (const std::exception& Error) {
                std::cerr << "Cleanup failed: " << Error.what() << "\n";
            }
            catch (...) {
                std::cerr << "Cleanup failed: unknown exception\n";
            }
        } else {
            Failure = std::current_exception();
        }
    }

    if (Failure) {
        try {
            std::rethrow_exception(Failure);
        }
        catch (const std::exception& Error) {
            std::cerr << Error.what() << "\n";
        }
        catch (...) {
            std::cerr << "Unknown engine exception\n";
        }
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

void StartUp() { BootstrapManager::It::InitAll(); }

void CleanUp() { BootstrapManager::It::CleanAll(); }

void TickFrame()
{
    Util::Timer::NextFrame();

    JobSystem::It::RunAll();
    JobSystem::It::WaitAll();
}

}
