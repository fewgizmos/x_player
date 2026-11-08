#pragma once

#include "X/Engine/Base/BootstrapManager/Bootstrap.hpp"

#include <vector>

namespace Engine::Base::BootstrapManager::DeferredBootstrapRegistration
{

void AddInit(std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);
void AddClean(std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);

std::vector<Entry> GetInitEntries();
std::vector<Entry> GetCleanEntries();

class InitRegistrar
{
public:
    InitRegistrar(std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);
};

class CleanRegistrar
{
public:
    CleanRegistrar(std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);
};

}
