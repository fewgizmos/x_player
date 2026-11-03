#pragma once

#include "X/Engine/Base/JobSystem/Job.hpp"

#include <string>

namespace Engine::Base::JobSystem::It
{

JobThread& CreateThread(Tag p_Tag);
JobThread& AttachCurrentThread(Tag p_Tag);

JobThread& GetThread(Tag p_Tag);

void AddTask(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);
void RemoveTask(const std::string& p_Name);

void PushBackground(Tag p_Tag, std::string p_Name, Task p_Func, int p_Order = DEFAULT_ORDER);

void RunAll();
void WaitAll();

void Shutdown();

}
