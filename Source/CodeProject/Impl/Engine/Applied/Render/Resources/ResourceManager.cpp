#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace Engine::Applied::Render::Resources
{

namespace
{

std::mutex m_ResourcesMutex;
std::unordered_map<std::string, std::weak_ptr<Resource>> m_Resources;

std::string MakeCacheKey(const Resource& p_Resource)
{
    return p_Resource.GetTypeName() + ":" + p_Resource.GetKey();
}

}

Resource::Resource(std::string p_Key)
    : m_Key(std::move(p_Key))
{
}

Resource::~Resource()
{
}

const std::string& Resource::GetKey() const
{
    return m_Key;
}

ResourceState Resource::GetState() const
{
    return m_State.load();
}

bool Resource::IsReady() const
{
    return GetState() == ResourceState::Ready;
}

void Resource::SetState(ResourceState p_State)
{
    m_State = p_State;
}

std::shared_ptr<Resource> Acquire(std::unique_ptr<Resource> p_Resource)
{
    const std::string CacheKey = MakeCacheKey(*p_Resource);

    std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

    if (const auto Found = m_Resources.find(CacheKey); Found != m_Resources.end()) {
        if (std::shared_ptr<Resource> Existing = Found->second.lock()) {
            return Existing;
        }

        m_Resources.erase(Found);
    }

    std::shared_ptr<Resource> Shared(
        p_Resource.release(),
        [](Resource* p_Resource) {
            p_Resource->SetState(ResourceState::Unloading);
            p_Resource->Unload();
            delete p_Resource;
        }
    );
    m_Resources.emplace(CacheKey, Shared);
    Shared->BeginLoad();

    return Shared;
}

void CleanExpired()
{
    std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

    for (auto It = m_Resources.begin(); It != m_Resources.end();) {
        if (It->second.expired()) {
            It = m_Resources.erase(It);
        } else {
            ++It;
        }
    }
}

}
