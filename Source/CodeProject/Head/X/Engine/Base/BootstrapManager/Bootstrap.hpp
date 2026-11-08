#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Engine::Base::BootstrapManager
{

using Task = std::function<void()>;

inline constexpr int DEFAULT_ORDER = 50;

struct Entry
{
    std::string Name;
    int Order = DEFAULT_ORDER;
    Task Func;
    std::uint64_t Sequence = 0;
};

}
