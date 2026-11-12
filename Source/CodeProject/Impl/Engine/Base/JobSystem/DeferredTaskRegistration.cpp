#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include <functional>
#include <mutex>
#include <utility>
#include <vector>

namespace Engine::Base::JobSystem::DeferredTaskRegistration
{

namespace
{

const BootstrapManager::DeferredBootstrapRegistration::InitRegistrar
    RegisterAppling("jobsystem_deferred_task_appling", Apply, 60);

struct Entry
{
    unsigned char Thread;
    std::string Name;
    std::function<void()> Callback;
    int Order;
};

std::vector<Entry>& Entries()
{
    static std::vector<Entry> Value;
    return Value;
}

std::mutex& EntriesMutex()
{
    static std::mutex Value;
    return Value;
}

bool& Applied()
{
    static bool Value = false;
    return Value;
}

}

void Add(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order)
{
    std::lock_guard<std::mutex> Lock(EntriesMutex());
    Entries().push_back({p_Thread, std::move(p_Name), std::move(p_Callback), p_Order});
}

void Apply()
{
    std::lock_guard<std::mutex> Lock(EntriesMutex());

    if (Applied()) {
        return;
    }

    for (Entry& Item : Entries()) {
        It::AddTask(Item.Thread, std::move(Item.Name), std::move(Item.Callback), Item.Order);
    }

    Entries().clear();
    Applied() = true;
}

Registrar::Registrar(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order)
{
    Add(p_Thread, std::move(p_Name), std::move(p_Callback), p_Order);
}

}
