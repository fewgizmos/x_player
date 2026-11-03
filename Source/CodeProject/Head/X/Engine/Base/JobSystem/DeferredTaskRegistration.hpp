#pragma once

#include "X/Engine/Base/JobSystem/Job.hpp"

#include <string>

namespace Engine::Base::JobSystem::DeferredTaskRegistration
{

void Add(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order);
void Apply();

class Registrar
{
public:
    Registrar(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order);
};

}
