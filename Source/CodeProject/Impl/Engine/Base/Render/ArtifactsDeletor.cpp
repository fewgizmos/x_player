#include "X/Engine/Base/Render/ArtifactsDeletor.hpp"

#include <exception>
#include <utility>
#include <vector>

namespace Engine::Base::Render::ArtifactsDeletor
{

namespace
{

std::vector<std::function<void()>> m_Deletors;

}

void Push(std::function<void()> p_Callback)
{
    m_Deletors.push_back(std::move(p_Callback));
}

void CleanAll()
{
    std::exception_ptr Failure;

    while (!m_Deletors.empty()) {
        auto Callback = std::move(m_Deletors.back());

        m_Deletors.pop_back();

        try {
        Callback();
        }

        catch (...) {
            if (!Failure) {
                Failure = std::current_exception();
            }
        }
    }

    if (Failure) {
        std::rethrow_exception(Failure);
    }
}

}
