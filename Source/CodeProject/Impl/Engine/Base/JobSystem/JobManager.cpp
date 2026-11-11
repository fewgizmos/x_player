#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"
#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

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
    AttachCurrentThread(::Engine::Base::Runtime::EngineThreadID::GameTh);
    CreateThread(::Engine::Base::Runtime::EngineThreadID::RenderTh);
    CreateThread(::Engine::Base::Runtime::EngineThreadID::ResourceLoaderTh);

    DeferredTaskRegistration::Apply();
}

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterInit("jobsystem_init", Init, 60);
const BootstrapManager::DeferredBootstrapRegistration::CleanRegistrar
    RegisterClean("jobsystem_clean", Shutdown, 100);

FrameSync m_Sync;

std::array<std::unique_ptr<JobThread>, 256> m_ThreadsByIndex;

std::vector<JobThread*> m_Threads;

JobThread* m_Attached = nullptr;

std::mutex m_NamesMutex;
std::unordered_map<std::string, unsigned char> m_TaskNames;

bool m_FrameActive = false;

std::string DescribeThread(unsigned char p_Thread)
{
    return std::to_string(static_cast<int>(p_Thread));
}

JobThread& Register(unsigned char p_Thread, bool p_IsAttached)
{
    X_ASSERT(m_ThreadsByIndex[p_Thread] != nullptr, "Job thread already exists: " + DescribeThread(p_Thread));

    m_ThreadsByIndex[p_Thread] = std::make_unique<JobThread>(p_Thread, p_IsAttached, m_Sync);

    m_Threads.push_back(m_ThreadsByIndex[p_Thread].get());

    return *m_ThreadsByIndex[p_Thread];
}

}

JobThread& CreateThread(unsigned char p_Thread)
{
    return Register(p_Thread, false);
}

JobThread& AttachCurrentThread(unsigned char p_Thread)
{
    X_ASSERT(m_Attached != nullptr, "Only one thread can be attached to the job system");

    JobThread& Thread = Register(p_Thread, true);

    m_Attached = &Thread;

    return Thread;
}

JobThread& GetThread(unsigned char p_Thread)
{
    JobThread* Thread = m_ThreadsByIndex[p_Thread].get();

    X_ASSERT(Thread == nullptr, "Job thread does not exist: " + DescribeThread(p_Thread));

    return *Thread;
}

void AddTask(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order)
{
    JobThread& Thread = GetThread(p_Thread);

    {
        std::lock_guard<std::mutex> Lock(m_NamesMutex);

        const bool Inserted = m_TaskNames.emplace(p_Name, p_Thread).second;

        X_ASSERT(!Inserted, "Job name is already used: " + p_Name);
    }

    Job Item;
    Item.Name  = std::move(p_Name);
    Item.Order = p_Order;
    Item.Callback = std::move(p_Callback);

    Thread.QueueAdd(std::move(Item));
}

void RemoveTask(const std::string& p_Name)
{
    unsigned char Owner = 0;

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

void PushBackground(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order)
{
    Job Item;
    Item.Name  = std::move(p_Name);
    Item.Order = p_Order;
    Item.Callback = std::move(p_Callback);

    GetThread(p_Thread).PushBackground(std::move(Item));
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
    for (std::unique_ptr<JobThread>& Thread : m_ThreadsByIndex) {
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
