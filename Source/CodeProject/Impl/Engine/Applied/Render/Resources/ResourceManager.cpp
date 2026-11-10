#include "X/Engine/Applied/Render/Resources/ResourceManager.hpp"

#include "X/Configs/PreCompiled/Dynamic.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Applied::Render::Resources
{

namespace
{

std::mutex m_ResourcesMutex;
std::unordered_map<std::string, std::weak_ptr<Resource>> m_Resources;

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterUpdate(
        ::Engine::Base::Runtime::EngineCore::RenderTh,
        "resources_update",
        UpdateAll,
        60
    );

std::string MakeCacheKey(const Resource& p_Resource)
{
    return p_Resource.GetTypeName() + ":" + p_Resource.GetKey();
}

}

Resource::Resource(std::string p_Key)
    : m_Key(std::move(p_Key))
{
}

Resource::~Resource() {}

const std::string& Resource::GetKey() const
{
    return m_Key;
}

ResourceState Resource::GetState() const
{
    return m_State.load();
}

void Resource::Update() {}

bool Resource::IsReady() const
{
    return GetState() == ResourceState::Ready;
}

const std::string& Resource::GetFailureReason() const
{
    return FailureReason;
}

void Resource::SetState(ResourceState p_State)
{
    m_State = p_State;
}

void Resource::SetFailed(std::string p_Reason)
{
    FailureReason = std::move(p_Reason);
    m_State = ResourceState::Failed;
}

bool Resource::TryMarkFailureReported()
{
    bool Expected = false;
    return FailureReported.compare_exchange_strong(Expected, true);
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

void UpdateAll()
{
    std::vector<std::shared_ptr<Resource>> Active;

    {
        std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

        for (auto It = m_Resources.begin(); It != m_Resources.end();) {
            if (std::shared_ptr<Resource> Resource = It->second.lock()) {
                Active.push_back(std::move(Resource));
                ++It;
            } else {
                It = m_Resources.erase(It);
            }
        }
    }

    for (const std::shared_ptr<Resource>& Resource : Active) {
        Resource->Update();

#ifdef XP_DEV_FEATURES_ENABLED
        if (Resource->GetState() == ResourceState::Failed && Resource->TryMarkFailureReported()) {
            std::cerr << "Resource failed: "
                      << Resource->GetTypeName() << ":" << Resource->GetKey();

            if (!Resource->GetFailureReason().empty()) {
                std::cerr << ": " << Resource->GetFailureReason();
            }

            std::cerr << "\n";
        }
#endif
    }
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
