#pragma once

#include <type_traits>

namespace Engine::Util::StateMachine
{

template <typename EnumType>
class State
{
public:
    using Storage = typename std::underlying_type<EnumType>::type;

    State() = default;
    explicit State(EnumType p_InitialState)
        : m_State(static_cast<Storage>(p_InitialState)) {}

    void SetState(EnumType p_StateFlag)    { m_State |= static_cast<Storage>(p_StateFlag); }
    void ToggleState(EnumType p_StateFlag) { m_State ^= static_cast<Storage>(p_StateFlag); }

    void ClearState(EnumType p_StateFlag)
    {
        m_State &= static_cast<Storage>(~static_cast<Storage>(p_StateFlag));
    }

    bool IsStateSet(EnumType p_StateFlag) const
    {
        return (m_State & static_cast<Storage>(p_StateFlag)) != 0;
    }

    void ClearAllStates() { m_State = 0; }

    Storage GetRaw() const { return m_State; }

private:
    Storage m_State = 0;
};

}
