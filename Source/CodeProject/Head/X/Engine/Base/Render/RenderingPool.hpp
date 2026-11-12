#pragma once

#include <functional>
#include <string>

namespace Engine::Base::Render::RenderingPool
{

using RenderCallback = std::function<void()>;
using SnapshotCallback = std::function<void()>;

void Register(std::string p_Name, RenderCallback p_Callback, int p_Order = 50);
void RegisterSnapshot(std::string p_Name, SnapshotCallback p_Callback, int p_Order = 50);
void Seal();
void CaptureFrameSnapshot();
void RenderAll();
void Clear();

}
