#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineCore.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine::Base::JobSystem::It
{

namespace
{

void Init()
{
    AttachCurrentThread(Runtime::EngineCore::GameTh);
    CreateThread(Runtime::EngineCore::RenderTh);
    CreateThread(Runtime::EngineCore::ResourceLoaderTh);

    DeferredTaskRegistration::Apply();
}

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("jobsystem_init", Init, 60);
const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("jobsystem_clean", Shutdown, 100);

FrameSync m_Sync;

std::array<std::unique_ptr<JobThread>, 256> m_ThreadsByTag;

std::vector<JobThread*> m_Threads;

JobThread* m_Attached = nullptr;

std::mutex m_NamesMutex;
std::unordered_map<std::string, Tag> m_TaskNames;

bool m_FrameActive = false;

std::string DescribeTag(Tag p_Tag)
{
    return std::to_string(static_cast<int>(p_Tag));
}

JobThread& Register(Tag p_Tag, bool p_IsAttached)
{
    X_ASSERT(m_ThreadsByTag[p_Tag] != nullptr, "Job thread already exists: " + DescribeTag(p_Tag));

    m_ThreadsByTag[p_Tag] = std::make_unique<JobThread>(p_Tag, p_IsAttached, m_Sync);

    m_Threads.push_back(m_ThreadsByTag[p_Tag].get());

    return *m_ThreadsByTag[p_Tag];
}

}

JobThread& CreateThread(Tag p_Tag)
{
    return Register(p_Tag, false);
}

JobThread& AttachCurrentThread(Tag p_Tag)
{
    X_ASSERT(m_Attached != nullptr, "Only one thread can be attached to the job system");

    JobThread& Thread = Register(p_Tag, true);

    m_Attached = &Thread;

    return Thread;
}

JobThread& GetThread(Tag p_Tag)
{
    JobThread* Thread = m_ThreadsByTag[p_Tag].get();

    X_ASSERT(Thread == nullptr, "Job thread does not exist: " + DescribeTag(p_Tag));

    return *Thread;
}

void AddTask(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order)
{
    JobThread& Thread = GetThread(p_Tag);

    {
        std::lock_guard<std::mutex> Lock(m_NamesMutex);

        const bool Inserted = m_TaskNames.emplace(p_Name, p_Tag).second;

        X_ASSERT(!Inserted, "Job name is already used: " + p_Name);
    }

    Job Item;
    Item.Name  = std::move(p_Name);
    Item.Order = p_Order;
    Item.Func  = std::move(p_Func);

    Thread.QueueAdd(std::move(Item));
}

void RemoveTask(const std::string& p_Name)
{
    Tag Owner = 0;

    {
        std::lock_guard<std::mutex> Lock(m_NamesMutex);

        const auto Found = m_TaskNames.find(p_Name);

        if (Found == m_TaskNames.end()) {
            return;
        }

        Owner = Found->second;

        m_TaskNames.erase(Found);
    }

    GetThread(Owner).QueueRemove(p_Name);
}

void PushBackground(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order)
{
    Job Item;
    Item.Name  = std::move(p_Name);
    Item.Order = p_Order;
    Item.Func  = std::move(p_Func);

    GetThread(p_Tag).PushBackground(std::move(Item));
}

void RunAll()
{
    X_ASSERT(m_FrameActive, "RunAll was called before WaitAll of the previous frame");

    X_ASSERT(m_Attached != nullptr && m_Attached->GetThreadId() != std::this_thread::get_id(), "RunAll must be called from the attached thread");

    m_FrameActive = true;

    int Workers = 0;

    for (JobThread* Thread : m_Threads) {
        Thread->ApplyPending();

        if (!Thread->IsAttached() && Thread->HasFrameTasks()) {
            ++Workers;
        }
    }

    bool Failed = false;

    {
        std::lock_guard<std::mutex> Lock(m_Sync.Mutex);

        Failed = static_cast<bool>(m_Sync.Failure);

        m_Sync.Pending = Failed ? 0 : Workers;
    }

    if (Failed) {
        return;
    }

    for (JobThread* Thread : m_Threads) {
        if (!Thread->IsAttached() && Thread->HasFrameTasks()) {
            Thread->StartFrame();
        }
    }

    if (m_Attached != nullptr) {
        m_Attached->RunFrame();
    }
}

void WaitAll()
{
    X_ASSERT(!m_FrameActive, "WaitAll was called without RunAll");

    std::exception_ptr Failure;

    {
        std::unique_lock<std::mutex> Lock(m_Sync.Mutex);

        m_Sync.Cv.wait(Lock, [] { return m_Sync.Pending == 0; });

        Failure = m_Sync.Failure;
    }

    m_FrameActive = false;

    if (Failure) {
        std::rethrow_exception(Failure);
    }
}

void Shutdown()
{
    for (std::unique_ptr<JobThread>& Thread : m_ThreadsByTag) {
        Thread.reset();
    }

    m_Threads.clear();

    m_Attached = nullptr;

    {
        std::lock_guard<std::mutex> Lock(m_NamesMutex);

        m_TaskNames.clear();
    }

    {
        std::lock_guard<std::mutex> Lock(m_Sync.Mutex);

        m_Sync.Failure = nullptr;
        m_Sync.Pending = 0;
    }

    m_Sync.Stop.store(false);

    m_FrameActive = false;
}

}
