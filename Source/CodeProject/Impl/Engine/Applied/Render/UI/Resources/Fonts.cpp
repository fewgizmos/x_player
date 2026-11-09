#include "X/Engine/Applied/Render/UI/Resources/Fonts.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::UI::Resources::Fonts
{

namespace
{

struct FontSource
{
    std::string Path;
    float Size = 0.0f;
    ImFontConfig Config;
    ImWchar GlyphRanges[3] = { 0x0400, 0x052F, 0 };
};

struct FontGroup
{
    std::string Name;
    float Size = 0.0f;
    std::vector<FontSource> MergedFonts;
    ImFont* Font = nullptr;
};

std::vector<FontGroup> m_Groups;
std::string m_ActiveGroup;
bool m_Initialized = false;

constexpr std::string_view DEFAULT_GROUP_NAME = "Default";
constexpr float DEFAULT_FONT_SIZE = 18.0f;
constexpr std::string_view DEFAULT_CYRILLIC_FONT_PATH = "Resources/Fonts/NotoSans-Regular.ttf";

ImFont* AddDefaultFont(FontGroup& p_Group)
{
    ImFontConfig Config;
    Config.SizePixels = p_Group.Size;
    Config.OversampleH = 2;
    Config.OversampleV = 1;

    return ImGui::GetIO().Fonts->AddFontDefault(&Config);
}

void AddMergedFonts(FontGroup& p_Group)
{
    for (FontSource& Source : p_Group.MergedFonts) {
        Source.Config.MergeMode = true;
        Source.Config.SizePixels = Source.Size;

        ImFont* Font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
            Source.Path.c_str(),
            Source.Size,
            &Source.Config,
            Source.GlyphRanges
        );

        X_ASSERT(Font == nullptr, "Failed to add merged font: " + Source.Path);
    }
}

FontGroup* FindGroup(std::string_view p_Name)
{
    const auto Found = std::find_if(
        m_Groups.begin(),
        m_Groups.end(),
        [p_Name](const FontGroup& Group) { return Group.Name == p_Name; }
    );

    return Found == m_Groups.end() ? nullptr : &*Found;
}

}

void Init()
{
    X_ASSERT(m_Initialized, "Fonts are already initialized");

    m_Initialized = true;

    AddGroup(std::string(DEFAULT_GROUP_NAME), DEFAULT_FONT_SIZE);
    AddMergedFont(DEFAULT_GROUP_NAME, std::string(DEFAULT_CYRILLIC_FONT_PATH), DEFAULT_FONT_SIZE);

    RebuildAtlas();
}

void Cleanup()
{
    if (!m_Initialized) {
        return;
    }

    m_Groups.clear();
    m_ActiveGroup.clear();
    m_Initialized = false;
}

void AddGroup(std::string p_Name, float p_Size)
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before adding a group");
    X_ASSERT(p_Name.empty(), "Font group name is empty");
    X_ASSERT(FindGroup(p_Name) != nullptr, "Font group already exists: " + p_Name);
    X_ASSERT(p_Size <= 0.0f, "Font group size must be positive");

    m_Groups.push_back({ std::move(p_Name), p_Size });

    if (m_ActiveGroup.empty()) {
        m_ActiveGroup = m_Groups.back().Name;
    }
}

void AddMergedFont(std::string_view p_GroupName, std::string p_FontPath, float p_Size)
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before adding a font");

    FontGroup* Group = FindGroup(p_GroupName);

    X_ASSERT(Group == nullptr, "Font group does not exist: " + std::string(p_GroupName));
    X_ASSERT(p_FontPath.empty(), "Font path is empty");
    X_ASSERT(!std::filesystem::exists(p_FontPath), "Font file does not exist: " + p_FontPath);
    X_ASSERT(p_Size <= 0.0f, "Font size must be positive");

    Group->MergedFonts.push_back({ std::move(p_FontPath), p_Size });
}

void RemoveGroup(std::string_view p_Name)
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before removing a group");

    const auto Found = std::find_if(
        m_Groups.begin(),
        m_Groups.end(),
        [p_Name](const FontGroup& Group) { return Group.Name == p_Name; }
    );

    if (Found == m_Groups.end()) {
        return;
    }

    const bool WasActive = Found->Name == m_ActiveGroup;
    m_Groups.erase(Found);

    if (WasActive) {
        m_ActiveGroup = m_Groups.empty() ? std::string() : m_Groups.front().Name;
    }
}

void RemoveAllGroups()
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before removing groups");

    m_Groups.clear();
    m_ActiveGroup.clear();
}

void SetActiveGroup(std::string_view p_Name)
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before selecting a group");
    X_ASSERT(FindGroup(p_Name) == nullptr, "Font group does not exist: " + std::string(p_Name));

    m_ActiveGroup = p_Name;
}

std::string_view GetActiveGroup()
{
    return m_ActiveGroup;
}

ImFont* GetFont(std::string_view p_Name)
{
    FontGroup* Group = FindGroup(p_Name);

    X_ASSERT(Group == nullptr, "Font group does not exist: " + std::string(p_Name));

    return Group->Font;
}

void RebuildAtlas()
{
    X_ASSERT(!m_Initialized, "Fonts must be initialized before rebuilding the atlas");

    ImFontAtlas* Atlas = ImGui::GetIO().Fonts;
    Atlas->Clear();

    for (FontGroup& Group : m_Groups) {
        Group.Font = AddDefaultFont(Group);
        AddMergedFonts(Group);
    }

    Atlas->Build();
}

}
