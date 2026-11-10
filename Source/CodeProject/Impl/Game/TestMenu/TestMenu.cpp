#include "X/Engine/Util/Timer.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include <imgui.h>

namespace Engine::Game::TestMenu
{

namespace
{

bool m_ShowMenu = true;

void Draw()
{
    if (!m_ShowMenu) {
        return;
    }

    ImGui::Begin("Test Menu", &m_ShowMenu);

    ImGui::Text("ImGui renderer is working.");
    ImGui::Text("FPS: %.1f", Util::Timer::FPS);
    ImGui::Text("Frame time: %.2f ms", Util::Timer::DeltaMs);

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text("Русский ТеКсТ 123 _*/:?^");

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Close application")) {
        Engine::Base::Platform::SDL3App::WantsExit = true;
    }

    ImGui::End();
}

const Base::JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    Base::Runtime::EngineCore::GameTh,
    "test_menu_draw",
    Draw,
    45
);

}

}
