#include "X/Engine/Base/Platform/SDL3WM.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include "X/Engine/Base/Platform/SDL3App.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace Engine::Base::Platform::SDL3WM
{

namespace
{

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("sdl3wm_clean", Cleanup, 20);

const JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh, "sdl3wm_events_pump", PumpEventsToWindows, 80
);

}

std::vector<std::unique_ptr<Window>> m_Windows;

Window* CreateWindow(const WindowCreateInfo& p_CreateInfo)
{
    SDL_WindowFlags Flags = SDL_WINDOW_HIDDEN
        | SDL_WINDOW_HIGH_PIXEL_DENSITY
        | p_CreateInfo.ExtraSDLFlags;

    if (p_CreateInfo.IsFullscreen) { Flags |= SDL_WINDOW_FULLSCREEN; }
    if (p_CreateInfo.IsResizable)  { Flags |= SDL_WINDOW_RESIZABLE;  }
    if (!p_CreateInfo.IsBordered)  { Flags |= SDL_WINDOW_BORDERLESS; }

    SDL_Window* Handle = SDL_CreateWindow(
        p_CreateInfo.Title.c_str(),
        p_CreateInfo.Width,
        p_CreateInfo.Height,
        Flags
    );

    X_ASSERT(Handle == nullptr, SDL_GetError());

    SDL_SetWindowPosition(Handle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    m_Windows.push_back(std::make_unique<Window>());

    Window* Created = m_Windows.back().get();
    Created->Handle = Handle;

    if (p_CreateInfo.IsCursorCaptured) {
        CaptureCursor(Created);
    }

    if (p_CreateInfo.IsVisible) {
        SDL_ShowWindow(Handle);
    }

    return Created;
}

void DestroyWindow(Window* p_Win)
{
    if (p_Win == nullptr) {
        return;
    }

    const auto Stored = std::find_if(
        m_Windows.begin(),
        m_Windows.end(),
        [p_Win](const std::unique_ptr<Window>& Candidate) { return Candidate.get() == p_Win; }
    );

    if (Stored == m_Windows.end()) {
        return;
    }

    SDL_DestroyWindow(p_Win->Handle);

    m_Windows.erase(Stored);
}

Window* GetWindowById(SDL_WindowID p_Id)
{
    for (const std::unique_ptr<Window>& Stored : m_Windows) {
        if (GetId(Stored.get()) == p_Id) {
            return Stored.get();
        }
    }

    return nullptr;
}

void PumpEventsToWindows()
{
    for (const SDL_Event& Event : SDL3App::GetEvents()) {
        if (Event.type < SDL_EVENT_WINDOW_FIRST || Event.type > SDL_EVENT_WINDOW_LAST) {
            continue;
        }

        Window* Win = GetWindowById(Event.window.windowID);

        if (Win == nullptr) {
            continue;
        }

        if (Event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            Win->WantsClose = true;
        }

        if (Event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            Win->UnhandleResized = true;
        }
    }
}

void Cleanup()
{
    for (const std::unique_ptr<Window>& Stored : m_Windows) {
        SDL_DestroyWindow(Stored->Handle);
    }

    m_Windows.clear();
}

glm::ivec2 GetExtent(Window* p_Win)
{
    glm::ivec2 Result(0);

    SDL_GetWindowSizeInPixels(p_Win->Handle, &Result.x, &Result.y);

    return Result;
}

void CaptureCursor(Window* p_Win)
{
    const bool Captured = SDL_SetWindowRelativeMouseMode(p_Win->Handle, true);

    X_ASSERT(!Captured, SDL_GetError());
}

void ReleaseCursor(Window* p_Win)
{
    const bool Released = SDL_SetWindowRelativeMouseMode(p_Win->Handle, false);

    X_ASSERT(!Released, SDL_GetError());
}

}
