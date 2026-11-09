#include "X/Engine/Applied/Render/UI/ImGuiScaler.hpp"

#include "X/Configs/Static.hpp"
#include "X/Engine/Base/Viewport/Surface.hpp"

#include <SDL3/SDL_video.h>
#include <imgui.h>

#include <algorithm>

namespace Engine::Applied::Render::UI::ImGuiScaler
{

namespace
{

ImGuiStyle m_BaseStyle;
float m_LastScale = -1.0f;

}

void Init()
{
    m_BaseStyle = ImGui::GetStyle();
    m_LastScale = -1.0f;
    Update();
}

void Update()
{
    const auto Extent = ::Engine::Base::Viewport::Surface::GetExtent();

    if (Extent.x <= 0 || Extent.y <= 0) {
        return;
    }

    const float WidthScale = static_cast<float>(Extent.x)
        / static_cast<float>(::Engine::Configs::Static::Viewport::WINDOW_WIDTH);
    const float HeightScale = static_cast<float>(Extent.y)
        / static_cast<float>(::Engine::Configs::Static::Viewport::WINDOW_HEIGHT);
    const float DisplayScale = SDL_GetWindowDisplayScale(
        ::Engine::Base::Viewport::Surface::GetNative()
    );
    const float DpiScale = DisplayScale > 0.0f ? DisplayScale : 1.0f;
    const float Scale = std::max(1.0f, std::min(WidthScale, HeightScale) * DpiScale);

    if (Scale == m_LastScale) {
        return;
    }

    ImGuiStyle& Style = ImGui::GetStyle();

    Style = m_BaseStyle;
    Style.ScaleAllSizes(Scale);
    Style.FontScaleMain = Scale;
    Style.FontScaleDpi = 1.0f;

    if (m_BaseStyle.WindowBorderSize > 0.0f) {
        Style.WindowBorderSize = std::max(Style.WindowBorderSize, 1.0f);
    }

    if (m_BaseStyle.ChildBorderSize > 0.0f) {
        Style.ChildBorderSize = std::max(Style.ChildBorderSize, 1.0f);
    }

    if (m_BaseStyle.PopupBorderSize > 0.0f) {
        Style.PopupBorderSize = std::max(Style.PopupBorderSize, 1.0f);
    }

    if (m_BaseStyle.TabBarBorderSize > 0.0f) {
        Style.TabBarBorderSize = std::max(Style.TabBarBorderSize, 1.0f);
    }

    m_LastScale = Scale;
}

}
