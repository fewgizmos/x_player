#include "X/Engine/Applied/Render/SceneRendering/SceneList.hpp"

#include <atomic>
#include <utility>

namespace Engine::Applied::Render::SceneRendering::SceneList
{

namespace
{

std::atomic<std::shared_ptr<const Frame>> m_PublishedFrame;

}

void PublishFrame(std::shared_ptr<const Frame> p_Frame)
{
    m_PublishedFrame.store(std::move(p_Frame), std::memory_order_release);
}

std::shared_ptr<const Frame> Acquire()
{
    return m_PublishedFrame.load(std::memory_order_acquire);
}

}
