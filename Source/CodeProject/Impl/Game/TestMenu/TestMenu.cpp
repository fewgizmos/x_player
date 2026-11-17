#include "X/Engine/Util/Timer.hpp"

#include "X/Project/Configs/PreCompiled/Dynamic.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"
#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Util/Applied/ResourceManager.hpp"

#ifdef XP_DEV_FEATURES_ENABLED
#include "X/Engine/Applied/Render/Tests/ModelRenderer.hpp"
#endif

#include <imgui.h>

namespace Engine::Game::TestMenu
{

namespace
{

bool m_IsMenuOpen = true;

const char* GetResourceStateName(::Engine::Util::Applied::ResourceState p_State)
{
    switch (p_State) {
    case ::Engine::Util::Applied::ResourceState::LoadingCpu:
        return "LoadingCpu";
    case ::Engine::Util::Applied::ResourceState::QueuedGpu:
        return "QueuedGpu";
    case ::Engine::Util::Applied::ResourceState::Uploading:
        return "Uploading";
    case ::Engine::Util::Applied::ResourceState::Ready:
        return "Ready";
    case ::Engine::Util::Applied::ResourceState::Failed:
        return "Failed";
    case ::Engine::Util::Applied::ResourceState::Unloading:
        return "Unloading";
    case ::Engine::Util::Applied::ResourceState::Rejected:
        return "Rejected";
    }

    return "Unknown";
}

void Draw()
{
    if (!m_IsMenuOpen) {
        Engine::Base::Platform::SDL3App::WantsExit = true;
    }

    ImGui::Begin("Test Menu", &m_IsMenuOpen);

    ImGui::Text("ImGui renderer is working.");
    ImGui::Text("FPS: %.1f", Util::Timer::FPS);
    ImGui::Text("Frame time: %.2f ms", Util::Timer::DeltaMs);
#ifdef XP_DEV_FEATURES_ENABLED
    ImGui::Text(
        "Model: %s",
        GetResourceStateName(
            ::Engine::Applied::Render::Tests::ModelRenderer::GetModelState()
        )
    );
#endif

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
