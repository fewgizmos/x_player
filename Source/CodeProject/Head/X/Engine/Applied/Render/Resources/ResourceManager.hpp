#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace Engine::Applied::Render::Resources
{

enum class ResourceState
{
    Loading,
    Ready,
    Failed,
    Unloading
};

class Resource
{
public:
    explicit Resource(std::string p_Key);
    virtual ~Resource();

    Resource(const Resource&) = delete;
    Resource& operator=(const Resource&) = delete;

    const std::string& GetKey() const;
    virtual std::string GetTypeName() const = 0;
    ResourceState GetState() const;
    bool IsReady() const;

    virtual void BeginLoad() = 0;
    virtual void Update();
    virtual void Unload() = 0;
    void SetState(ResourceState p_State);

private:
    friend std::shared_ptr<Resource> Acquire(std::unique_ptr<Resource> p_Resource);

    std::string m_Key;
    std::atomic<ResourceState> m_State = ResourceState::Loading;
};

std::shared_ptr<Resource> Acquire(std::unique_ptr<Resource> p_Resource);
void UpdateAll();

void CleanExpired();

template<class T>
class Ref
{
    static_assert(std::is_base_of_v<Resource, T>, "Ref<T> requires a Resource type");

public:
    template<class... Arguments>
    explicit Ref(Arguments&&... p_Arguments)
        : m_Resource(std::dynamic_pointer_cast<T>(Acquire(
            std::make_unique<T>(std::forward<Arguments>(p_Arguments)...)
        )))
    {
    }

    Ref(const Ref&) = default;
    Ref(Ref&&) noexcept = default;
    Ref& operator=(const Ref&) = default;
    Ref& operator=(Ref&&) noexcept = default;

    T* Get() { return m_Resource.get(); }
    const T* Get() const { return m_Resource.get(); }

    T* operator->() { return Get(); }
    const T* operator->() const { return Get(); }

    ResourceState GetState() const { return m_Resource->GetState(); }
    bool IsReady() const { return m_Resource->IsReady(); }

private:
    std::shared_ptr<T> m_Resource;
};

}
