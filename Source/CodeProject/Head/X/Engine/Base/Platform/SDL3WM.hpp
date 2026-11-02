#pragma once

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

#include <glm/vec2.hpp>

#include <string>

namespace Engine::Base::Platform::SDL3WM
{

struct WindowCreateInfo
{
    std::string Title = "X";

    int Width  = 1280;
    int Height = 720;

    bool IsFullscreen = false;
    bool IsResizable  = true;

    bool IsBordered = true;
    bool IsVisible  = true;

    bool IsCursorCaptured = false;

    SDL_WindowFlags ExtraSDLFlags = 0;
};

struct Window
{
    SDL_Window* Handle = nullptr;

    bool WantsClose      = false;
    bool UnhandleResized = false;
};

Window* CreateWindow(const WindowCreateInfo& p_CreateInfo);
void DestroyWindow(Window* p_Win);

Window* GetWindowById(SDL_WindowID p_Id);

void PumpEventsToWindows();

void Cleanup();

glm::ivec2 GetExtent(Window* p_Win);

void CaptureCursor(Window* p_Win);
void ReleaseCursor(Window* p_Win);

inline SDL_Window* GetNative(Window* p_Win) { return p_Win->Handle; }
inline SDL_WindowID GetId(Window* p_Win) { return SDL_GetWindowID(p_Win->Handle); }

inline bool IsCursorCaptured(Window* p_Win) { return SDL_GetWindowRelativeMouseMode(p_Win->Handle); }

}
