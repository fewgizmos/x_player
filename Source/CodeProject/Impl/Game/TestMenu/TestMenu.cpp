#include "X/Engine/Util/Base/Timer.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"
#include "X/Engine/Base/Platform/SDL3App.hpp"

#include <imgui.h>

namespace Game::TestMenu
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
    ImGui::Text("FPS: %.1f", Engine::Util::Base::Timer::FPS);
    ImGui::Text("Frame time: %.2f ms", Engine::Util::Base::Timer::DeltaMs);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Русский ТеКсТ 123 _*/:?^");

    ImGui::End();
}

const Engine::Base::JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh,
    "test_game_menu_draw",
    Draw,
    45
);

}

}
