#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace Engine::Base::Render::RenderingPool
{

namespace
{

struct Entry
{
    std::string Name;
    RenderCallback Callback;
    int Order;
};

std::vector<Entry> m_Entries;

bool m_Sealed = false;

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("rendering_pool_seal", Seal, -100);

}

void Register(std::string p_Name, RenderCallback p_Callback, int p_Order)
{
    X_ASSERT(m_Sealed, "Rendering pool is already sealed");

    X_ASSERT(!p_Callback, "Empty renderer callback: " + p_Name);

    X_ASSERT(std::any_of(m_Entries.begin(), m_Entries.end(),
        [&](const Entry& Item) {
            return Item.Name == p_Name;
        }), "Duplicate renderer: " + p_Name);

    m_Entries.push_back({std::move(p_Name), std::move(p_Callback), p_Order});
}

void Seal()
{
    std::stable_sort(m_Entries.begin(), m_Entries.end(),
        [](const Entry& p_A, const Entry& B) {
            return p_A.Order > B.Order;
        });

    m_Sealed = true;
}

void RenderAll()
{
    X_ASSERT(!m_Sealed, "Rendering pool must be sealed before rendering");

    for (const auto& Item : m_Entries) {
        Item.Callback();
    }
}

void Clear()
{
    m_Entries.clear();
    m_Sealed = false;
}

}
