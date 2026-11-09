#pragma once

#include <imgui.h>

#include <string>
#include <string_view>

namespace Engine::Applied::Render::UI::Resources::Fonts
{

void Init();
void Cleanup();

void AddGroup(std::string p_Name, float p_Size);
void AddMergedFont(std::string_view p_GroupName, std::string p_FontPath, float p_Size);
void RemoveGroup(std::string_view p_Name);
void RemoveAllGroups();

void SetActiveGroup(std::string_view p_Name);
std::string_view GetActiveGroup();

ImFont* GetFont(std::string_view p_Name);

void RebuildAtlas();

}
