#include "X/Engine/Base/Viewport/Surface.hpp"

#include "X/Configs/PreCompiled/Dynamic.hpp"
#include "X/Configs/Static.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include "X/Engine/Base/Platform/SDL3WM.hpp"

#include <SDL3/SDL_pixels.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_video.h>

#include <stb_image.h>

#include <atomic>
#include <cstdint>

namespace Engine::Base::Viewport::Surface
{

namespace
{

Platform::SDL3WM::Window* m_Win = nullptr;

std::atomic<std::uint64_t> m_Extent = 0;
std::atomic<bool> m_Resized = false;

void UpdateWindowState()
{
    const auto Extent = Platform::SDL3WM::GetExtent(m_Win);

    m_Extent.store((static_cast<std::uint64_t>(Extent.x) << 32) | static_cast<std::uint32_t>(Extent.y));

    if (m_Win->UnhandleResized) {
        m_Resized.store(true);
        m_Win->UnhandleResized = false;
    }
}

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("surface_init", SetupForVulkan, 80);

const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("surface_clean", Destroy, 40);

const JobSystem::DeferredTaskRegistration::Registrar
    RegisterWindowState(Runtime::EngineCore::GameTh, "surface_window_state", UpdateWindowState, 60);

void ApplyWindowIcon()
{
    int LogoWidth    = 0;
    int LogoHeight   = 0;
    int LogoChannels = 0;

    stbi_uc* LogoPixels = stbi_load(X_WINDOW_LOGO_PATH, &LogoWidth, &LogoHeight, &LogoChannels, 4);

    SDL_Surface* LogoSurface = SDL_CreateSurfaceFrom(LogoWidth, LogoHeight, SDL_PIXELFORMAT_RGBA32, LogoPixels, LogoWidth * 4);

    SDL_SetWindowIcon(Platform::SDL3WM::GetNative(m_Win), LogoSurface);

    SDL_DestroySurface(LogoSurface);

    stbi_image_free(LogoPixels);
}

}

void SetupForVulkan()
{
    Platform::SDL3WM::WindowCreateInfo CreateInfo;

    CreateInfo.Title = X_WINDOW_NAME;

    CreateInfo.Width  = Configs::Static::Viewport::WINDOW_WIDTH;
    CreateInfo.Height = Configs::Static::Viewport::WINDOW_HEIGHT;

    CreateInfo.IsFullscreen = Configs::Static::Viewport::WINDOW_FULLSCREEN;
    CreateInfo.IsResizable  = Configs::Static::Viewport::WINDOW_RESIZABLE;

    CreateInfo.IsBordered = Configs::Static::Viewport::WINDOW_BORDERED;
    CreateInfo.IsVisible  = Configs::Static::Viewport::WINDOW_VISIBLE;

    CreateInfo.IsCursorCaptured = Configs::Static::Viewport::WINDOW_CURSOR_CAPTURED;

    CreateInfo.ExtraSDLFlags |= SDL_WINDOW_VULKAN;

    m_Win = Platform::SDL3WM::CreateWindow(CreateInfo);

    ApplyWindowIcon();

    UpdateWindowState();
}

void Destroy()
{
    Platform::SDL3WM::DestroyWindow(m_Win);

    m_Win = nullptr;
}

SDL_Window* GetNative() { return Platform::SDL3WM::GetNative(m_Win); }

glm::ivec2 GetExtent()
{
    const auto Extent = m_Extent.load();

    return {static_cast<int>(Extent >> 32), static_cast<int>(Extent & 0xFFFFFFFFu)};
}

bool GetWantsExit() { return m_Win->WantsClose; }

bool GetUnhandleResized() { return m_Resized.load(); }
void ClearUnhandleResized() { m_Resized.store(false); }

}
