#include "X/Engine/Util/Applied/ResourceManager.hpp"

#include "X/Project/Configs/PreCompiled/Dynamic.hpp"
#include "X/Project/Configs/Static.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"

#include "X/Engine/Base/Render/RenderingPool.hpp"

#include "X/Engine/Base/Runtime/BaseEngineThreadIDs.hpp"

#include <exception>
#include <iostream>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Util::Applied
{

namespace
{

std::mutex m_ResourcesMutex;

std::unordered_map<std::string, std::weak_ptr<Resource>> m_Resources;
std::vector<std::weak_ptr<Resource>>                     m_ManagedResources;

struct RetiredResource
{
    Resource* Target = nullptr;
    int FramesLeft = ::Engine::Configs::Static::Graphics::MAX_FRAMES_IN_FLIGHT;
};

std::mutex                   m_RetiredMutex;
std::vector<RetiredResource> m_Retired;

void Destroy(Resource* p_Resource)
{
    std::unique_ptr<Resource> Owner(p_Resource);
    Owner->Unload();
}

void CollectRetired()
{
    std::vector<Resource*> Ready;
    {
        std::lock_guard<std::mutex> Lock(m_RetiredMutex);
        for (auto It = m_Retired.begin(); It != m_Retired.end();) {
            if (It->FramesLeft-- > 0) {
                ++It;
            } else {
                Ready.push_back(It->Target);
                It = m_Retired.erase(It);
            }
        }
    }
    for (Resource* Target : Ready) {
        Destroy(Target);
    }
}

void Cleanup()
{
    std::vector<std::shared_ptr<Resource>> Active;
    {
        std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

        for (const std::weak_ptr<Resource>& Weak : m_ManagedResources) {
            if (const std::shared_ptr<Resource> ResourceValue = Weak.lock()) {
                Active.push_back(ResourceValue);
            }
        }

        m_Resources.clear();
        m_ManagedResources.clear();
    }

    for (const std::shared_ptr<Resource>& ResourceValue : Active) {
        ResourceValue->Unload();
    }

    while (true) {
        std::vector<RetiredResource> Retired;
        {
            std::lock_guard<std::mutex> Lock(m_RetiredMutex);
            Retired.swap(m_Retired);
        }
        if (Retired.empty()) {
            break;
        }
        for (const auto& Item : Retired) {
            Destroy(Item.Target);
        }
    }
}

void Init()
{
    ::Engine::Base::Render::RenderingPool::RegisterSnapshot("resources_collect_retired", CollectRetired, 1000);
}

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("resources_init", Init, -50);

const ::Engine::Base::BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("resources_cleanup", Cleanup, 76);

const ::Engine::Base::JobSystem::DeferredTaskRegistration::Registrar
    RegisterUpdate(
        ::Engine::Base::Runtime::EngineThreadID::RenderTh,
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

uint64_t Resource::GetVersionChecksum() const
{
    return m_VersionChecksum.load(std::memory_order_acquire);
}

uint64_t Resource::AdvanceVersionChecksum()
{
    return m_VersionChecksum.fetch_add(1, std::memory_order_acq_rel) + 1;
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

void Resource::SetRejected(std::string p_Reason)
{
    FailureReason = std::move(p_Reason);
    m_State = ResourceState::Rejected;
}

bool Resource::TryMarkFailureReported()
{
    bool Expected = false;

    return FailureReported.compare_exchange_strong(Expected, true);
}

std::shared_ptr<Resource> Acquire(std::unique_ptr<Resource> p_Resource, ResourceManageInfo p_ManageInfo)
{
    p_ManageInfo.UsingCache &= p_Resource->AllowsCaching();
    const std::string CacheKey = MakeCacheKey(*p_Resource);

    std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

    if (p_ManageInfo.UsingCache) {
        if (const auto Found = m_Resources.find(CacheKey); Found != m_Resources.end()) {
            if (std::shared_ptr<Resource> Existing = Found->second.lock()) {
                return Existing;
            }

            m_Resources.erase(Found);
        }
    }

    std::shared_ptr<Resource> Shared(
        p_Resource.release(),
        [](Resource* p_Resource) {
            std::lock_guard<std::mutex> Lock(m_RetiredMutex);
            m_Retired.push_back({ p_Resource });
        }
    );
    m_ManagedResources.push_back(Shared);

    if (p_ManageInfo.UsingCache) {
        m_Resources.emplace(CacheKey, Shared);
    }

    Shared->BeginLoad();

    return Shared;
}

void UpdateAll()
{
    std::vector<std::shared_ptr<Resource>> Active;

    {
        std::lock_guard<std::mutex> Lock(m_ResourcesMutex);

        for (auto It = m_ManagedResources.begin(); It != m_ManagedResources.end();) {
            if (std::shared_ptr<Resource> Resource = It->lock()) {
                Active.push_back(std::move(Resource));
                ++It;
            } else {
                It = m_ManagedResources.erase(It);
            }
        }
    }

    for (const std::shared_ptr<Resource>& Resource : Active) {
        try {
            Resource->Update();
        }
        catch (const std::exception& Error) {
            Resource->SetFailed(Error.what());
        }
        catch (...) {
            Resource->SetFailed("unknown resource update failure");
        }

#ifdef X_DEV_MODE
        if ((Resource->GetState() == ResourceState::Failed
                || Resource->GetState() == ResourceState::Rejected)
            && Resource->TryMarkFailureReported()) {
            std::cerr << "resource failed: "
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

    for (auto It = m_ManagedResources.begin(); It != m_ManagedResources.end();) {
        if (It->expired()) {
            It = m_ManagedResources.erase(It);
        } else {
            ++It;
        }
    }
}

}
