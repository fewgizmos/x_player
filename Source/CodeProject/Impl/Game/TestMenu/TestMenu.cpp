#include "X/Engine/Util/Timer.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"
#include "X/Engine/Base/Platform/SDL3App.hpp"

#include <imgui.h>

namespace Engine::Game::TestMenu
{

namespace
{

bool m_IsMenuOpen = true;

void Draw()
{
    if (!m_IsMenuOpen) {
        Engine::Base::Platform::SDL3App::WantsExit = true;
    }

    ImGui::Begin("Test Menu", &m_IsMenuOpen);

    ImGui::Text("ImGui renderer is working.");
    ImGui::Text("FPS: %.1f", Util::Timer::FPS);
    ImGui::Text("Frame time: %.2f ms", Util::Timer::DeltaMs);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Русский ТеКсТ 123 _*/:?^");

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::End();
}

const Base::JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh,
    "test_game_menu_draw",
    Draw,
    45
);

}

}
