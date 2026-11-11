#pragma once

#include "X/Engine/Base/JobSystem/Job.hpp"

#include <string>

namespace Engine::Base::JobSystem::It
{

JobThread& CreateThread(unsigned char p_Thread);
JobThread& AttachCurrentThread(unsigned char p_Thread);

JobThread& GetThread(unsigned char p_Thread);

void AddTask(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order = DEFAULT_ORDER);
void RemoveTask(const std::string& p_Name);

void PushBackground(unsigned char p_Thread, std::string p_Name, std::function<void()> p_Callback, int p_Order = DEFAULT_ORDER);

void RunAll();
void WaitAll();

void Shutdown();

}
