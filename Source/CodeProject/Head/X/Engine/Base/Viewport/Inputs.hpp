#pragma once

#include "X/Engine/Util/StateMachine.hpp"

namespace Engine::Base::Viewport::Inputs
{

enum class KeyboardInputCodes : unsigned long long
{
    F1 = 1ULL << 0,
    F2 = 1ULL << 1,
    F3 = 1ULL << 2,
    F4 = 1ULL << 3,
    F6 = 1ULL << 4,
    F8 = 1ULL << 5,

    Grave = 1ULL << 6,
    Esc   = 1ULL << 7,

    P = 1ULL << 8,
    Q = 1ULL << 9,

    W = 1ULL << 10,
    A = 1ULL << 11,
    S = 1ULL << 12,
    D = 1ULL << 13,

    Shift     = 1ULL << 14,
    Control   = 1ULL << 15,
    Space     = 1ULL << 16,
    Enter     = 1ULL << 17,
    Backspace = 1ULL << 18,
    Tab       = 1ULL << 19,
    Alt       = 1ULL << 20,

    Up    = 1ULL << 21,
    Down  = 1ULL << 22,
    Left  = 1ULL << 23,
    Right = 1ULL << 24,

    E = 1ULL << 25,
    R = 1ULL << 26,
    T = 1ULL << 27,
    F = 1ULL << 28,
    G = 1ULL << 29,
    V = 1ULL << 30,
    M = 1ULL << 31,
    H = 1ULL << 32,
    Z = 1ULL << 33,
    X = 1ULL << 34,
    I = 1ULL << 35,
    Y = 1ULL << 36,
    N = 1ULL << 37,
};

enum class MouseInputCodes : unsigned int
{
    Left   = 1U << 0,
    Middle = 1U << 1,
    Right  = 1U << 2,

    Side1 = 1U << 3,
    Side2 = 1U << 4,
};

struct Mouse
{
    float PosX = 0.0f;
    float PosY = 0.0f;

    float DeltaX = 0.0f;
    float DeltaY = 0.0f;

    float ScrollX = 0.0f;
    float ScrollY = 0.0f;

    Util::StateMachine::State<MouseInputCodes> ButtonsInputCodes{};
    Util::StateMachine::State<MouseInputCodes> PreviousButtonsInputCodes{};

    bool ButtonIsPressed(MouseInputCodes p_Code) const
    {
        return !PreviousButtonsInputCodes.IsStateSet(p_Code) && ButtonsInputCodes.IsStateSet(p_Code);
    }

    bool ButtonIsHeld(MouseInputCodes p_Code) const
    {
        return PreviousButtonsInputCodes.IsStateSet(p_Code) && ButtonsInputCodes.IsStateSet(p_Code);
    }

    bool ButtonIsReleased(MouseInputCodes p_Code) const
    {
        return PreviousButtonsInputCodes.IsStateSet(p_Code) && !ButtonsInputCodes.IsStateSet(p_Code);
    }

    bool ButtonIsUp(MouseInputCodes p_Code) const
    {
        return !PreviousButtonsInputCodes.IsStateSet(p_Code) && !ButtonsInputCodes.IsStateSet(p_Code);
    }
};

void ParseFrameInputs();

void SetCapture(bool p_Mouse, bool p_Keyboard);

bool KeyIsPressed(KeyboardInputCodes p_Code);
bool KeyIsHeld(KeyboardInputCodes p_Code);
bool KeyIsReleased(KeyboardInputCodes p_Code);
bool KeyIsUp(KeyboardInputCodes p_Code);

const Mouse& GetMouse();

}
