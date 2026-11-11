#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Engine::Base::BootstrapManager
{

inline constexpr int DEFAULT_ORDER = 50;

struct Entry
{
    std::string Name;
    int Order = DEFAULT_ORDER;
    std::function<void()> Callback;
    std::uint64_t Sequence = 0;
};

}
