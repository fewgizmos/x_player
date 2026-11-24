#include "X/Engine/Applied/Render/SceneRendering/SceneRenderPassPool.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include <algorithm>
#include <utility>

namespace Engine::Applied::Render::SceneRendering::SceneRenderPassPool
{

namespace
{

struct Entry
{
    std::string Name;
    Phase       Stage;
    RenderPass  Callback;

    int Order = 50;
};

std::vector<Entry> m_Entries;

bool m_Sealed = false;

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterSeal("scene_render_pass_pool_seal", Seal, 10);

}

void Register(std::string p_Name, Phase p_Phase, RenderPass p_Callback, int p_Order)
{
    X_ASSERT(m_Sealed, "scene render pass pool is already sealed");
    X_ASSERT(!p_Callback, "empty scene render pass callback: " + p_Name);
    X_ASSERT(std::any_of(m_Entries.begin(), m_Entries.end(),
        [&p_Name](const Entry& p_Entry) {
            return p_Entry.Name == p_Name;
        }),
        "duplicate scene render pass: " + p_Name);

    m_Entries.push_back({ std::move(p_Name), p_Phase, std::move(p_Callback), p_Order });
}

void Seal()
{
    std::stable_sort(m_Entries.begin(), m_Entries.end(),
        [](const Entry& p_Left, const Entry& p_Right) {
            return p_Left.Order > p_Right.Order;
        }
    );

    m_Sealed = true;
}

void RenderAll(Phase p_Phase, const SceneList::Frame& p_Frame)
{
    X_ASSERT(!m_Sealed, "scene render pass pool must be sealed before rendering");

    for (const Entry& EntryValue : m_Entries) {
        if (EntryValue.Stage == p_Phase) { EntryValue.Callback(p_Frame); }
    }
}

void Clear()
{
    m_Entries.clear();
    m_Sealed = false;
}

}
