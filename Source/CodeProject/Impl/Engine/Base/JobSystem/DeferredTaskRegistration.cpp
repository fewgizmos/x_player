#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"

#include "X/Engine/Base/JobSystem/JobManager.hpp"

#include <mutex>
#include <utility>
#include <vector>

namespace Engine::Base::JobSystem::DeferredTaskRegistration
{

namespace
{

struct Entry
{
    Tag TagValue;
    std::string Name;
    Task Func;
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

void Add(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order)
{
    std::lock_guard<std::mutex> Lock(EntriesMutex());
    Entries().push_back({p_Tag, std::move(p_Name), std::move(p_Func), p_Order});
}

void Apply()
{
    std::lock_guard<std::mutex> Lock(EntriesMutex());

    if (Applied()) {
        return;
    }

    for (Entry& Item : Entries()) {
        It::AddTask(Item.TagValue, std::move(Item.Name), std::move(Item.Func), Item.Order);
    }

    Entries().clear();
    Applied() = true;
}

Registrar::Registrar(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order)
{
    Add(p_Tag, std::move(p_Name), std::move(p_Func), p_Order);
}

}
