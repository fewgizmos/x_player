#pragma once

#include <functional>
#include <string>

namespace Engine::Base::JobSystem::DeferredTaskRegistration
{

void Add(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order);
void Apply();

class Registrar
{
public:
    Registrar(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order);
};

}
