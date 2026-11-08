#include "X/Engine/Base/BootstrapManager/DeferredBootstrapRegistration.hpp"

#include "X/Engine/Util/Base/Assert.hpp"

#include <algorithm>
#include <mutex>
#include <utility>

namespace Engine::Base::BootstrapManager::DeferredBootstrapRegistration
{

namespace
{

struct Registry
{
    std::mutex Mutex;

    std::vector<Entry> Init;
    std::vector<Entry> Clean;

    std::uint64_t NextSequence = 0;
};

Registry& GetRegistry()
{
    static Registry Value;

    return Value;
}

void Add(std::vector<Entry>& p_Entries, std::string p_Name, Task p_Func, int p_Order)
{
    X_ASSERT(!p_Func, "Bootstrap function is empty: " + p_Name);

    X_ASSERT(std::any_of(p_Entries.begin(), p_Entries.end(),
        [&p_Name](const Entry& Item) {
            return Item.Name == p_Name;
        }),
        "Bootstrap name is already used: " + p_Name);

    p_Entries.push_back({std::move(p_Name), p_Order, std::move(p_Func), GetRegistry().NextSequence++});
}

std::vector<Entry> Snapshot(const std::vector<Entry>& p_Entries)
{
    auto Entries = p_Entries;

    std::sort(Entries.begin(), Entries.end(), [](const Entry& A, const Entry& B) {
        return A.Order != B.Order ? A.Order > B.Order : A.Sequence < B.Sequence;
    });

    return Entries;
}

}

void AddInit(std::string p_Name, Task p_Func, int p_Order)
{
    auto& Registry = GetRegistry();

    std::lock_guard<std::mutex> Lock(Registry.Mutex);
    Add(Registry.Init, std::move(p_Name), std::move(p_Func), p_Order);
}

void AddClean(std::string p_Name, Task p_Func, int p_Order)
{
    auto& Registry = GetRegistry();

    std::lock_guard<std::mutex> Lock(Registry.Mutex);
    Add(Registry.Clean, std::move(p_Name), std::move(p_Func), p_Order);
}

std::vector<Entry> GetInitEntries()
{
    auto& Registry = GetRegistry();

    std::lock_guard<std::mutex> Lock(Registry.Mutex);

    return Snapshot(Registry.Init);
}

std::vector<Entry> GetCleanEntries()
{
    auto& Registry = GetRegistry();

    std::lock_guard<std::mutex> Lock(Registry.Mutex);

    return Snapshot(Registry.Clean);
}

InitRegistrar::InitRegistrar(std::string p_Name, Task p_Func, int p_Order)
{
    AddInit(std::move(p_Name), std::move(p_Func), p_Order);
}

CleanRegistrar::CleanRegistrar(std::string p_Name, Task p_Func, int p_Order)
{
    AddClean(std::move(p_Name), std::move(p_Func), p_Order);
}

}
