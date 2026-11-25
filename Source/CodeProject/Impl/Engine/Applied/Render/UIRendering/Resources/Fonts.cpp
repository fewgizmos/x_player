#include "X/Engine/Applied/Render/UIRendering/Resources/Fonts.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::UI::Resources::Fonts
{

void Init();

namespace
{

struct FontSource
{
    std::string Path;
    float Size = 0.0f;
    ImFontConfig Config;
    ImWchar GlyphRanges[3] = { 0x0400, 0x052F, 0 };
    ImWchar GlyphExcludeRanges[3] = { 0x0020, 0x024F, 0 };
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
        Source.Config.DstFont = p_Group.Font;
        Source.Config.GlyphRanges = Source.GlyphRanges;
        Source.Config.GlyphExcludeRanges = Source.GlyphExcludeRanges;
        Source.Config.GlyphOffset.y = -1.0f;

        ImFont* Font = ImGui::GetIO().Fonts->AddFontFromFileTTF(
            Source.Path.c_str(),
            Source.Size,
            &Source.Config,
            Source.GlyphRanges
        );

        X_ASSERT(Font == nullptr, "failed to add merged font: " + Source.Path);
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

void BuildStdGroup()
{
    AddGroup("Std", 13.05f);
    AddMergedFont("Std", "Resources/Fonts/IBMPlexSans-Regular.ttf", 15.05f);
    RebuildAtlas();
}

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterFontsInit("imgui_fonts_init", Init, 60);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterStdGroup("imgui_fonts_std_group", BuildStdGroup, 45);

}

void Init()
{
    X_ASSERT(m_Initialized, "fonts are already initialized");

    m_Initialized = true;
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
    X_ASSERT(!m_Initialized, "fonts must be initialized before adding a group");
    X_ASSERT(p_Name.empty(), "font group name is empty");
    X_ASSERT(FindGroup(p_Name) != nullptr, "font group already exists: " + p_Name);
    X_ASSERT(p_Size <= 0.0f, "font group size must be positive");

    m_Groups.push_back({ std::move(p_Name), p_Size });

    if (m_ActiveGroup.empty()) {
        m_ActiveGroup = m_Groups.back().Name;
    }
}

void AddMergedFont(std::string_view p_GroupName, std::string p_FontPath, float p_Size)
{
    X_ASSERT(!m_Initialized, "fonts must be initialized before adding a font");

    FontGroup* Group = FindGroup(p_GroupName);

    X_ASSERT(Group == nullptr, "font group does not exist: " + std::string(p_GroupName));
    X_ASSERT(p_FontPath.empty(), "font path is empty");
    X_ASSERT(!std::filesystem::exists(p_FontPath), "font file does not exist: " + p_FontPath);
    X_ASSERT(p_Size <= 0.0f, "font size must be positive");

    Group->MergedFonts.push_back({ std::move(p_FontPath), p_Size });
}

void RemoveGroup(std::string_view p_Name)
{
    X_ASSERT(!m_Initialized, "fonts must be initialized before removing a group");

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
    X_ASSERT(!m_Initialized, "fonts must be initialized before removing groups");

    m_Groups.clear();
    m_ActiveGroup.clear();
}

void SetActiveGroup(std::string_view p_Name)
{
    X_ASSERT(!m_Initialized, "fonts must be initialized before selecting a group");
    X_ASSERT(FindGroup(p_Name) == nullptr, "font group does not exist: " + std::string(p_Name));

    m_ActiveGroup = p_Name;
}

std::string_view GetActiveGroup()
{
    return m_ActiveGroup;
}

ImFont* GetFont(std::string_view p_Name)
{
    FontGroup* Group = FindGroup(p_Name);

    X_ASSERT(Group == nullptr, "font group does not exist: " + std::string(p_Name));

    return Group->Font;
}

void RebuildAtlas()
{
    X_ASSERT(!m_Initialized, "fonts must be initialized before rebuilding the atlas");

    ImFontAtlas* Atlas = ImGui::GetIO().Fonts;
    Atlas->Clear();

    for (FontGroup& Group : m_Groups) {
        Group.Font = AddDefaultFont(Group);
        AddMergedFonts(Group);
    }

    Atlas->Build();

    if (FontGroup* Group = FindGroup(m_ActiveGroup)) {
        ImGui::GetIO().FontDefault = Group->Font;
    }
}

}
