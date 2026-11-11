#include "X/Engine/Base/Viewport/Inputs.hpp"

#include "X/Engine/Base/JobSystem/DeferredTaskRegistration.hpp"
#include "X/Engine/Base/Platform/SDL3App.hpp"
#include "X/Engine/Base/Runtime/EngineThreadIDs.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace Engine::Base::Viewport::Inputs
{

namespace
{

const JobSystem::DeferredTaskRegistration::Registrar RegisterTask(
    ::Engine::Base::Runtime::EngineThreadID::GameTh, "inputs_pump", ParseFrameInputs, 70
);

Util::StateMachine::State<KeyboardInputCodes> m_PreviousKeys;
Util::StateMachine::State<KeyboardInputCodes> m_CurrentKeys;

Mouse m_Mouse;

bool m_MouseCaptured    = false;
bool m_KeyboardCaptured = false;

bool MapKey(SDL_Scancode p_Scancode, KeyboardInputCodes& p_Code)
{
    switch (p_Scancode) {
        case SDL_SCANCODE_F1: p_Code = KeyboardInputCodes::F1; return true;
        case SDL_SCANCODE_F2: p_Code = KeyboardInputCodes::F2; return true;
        case SDL_SCANCODE_F3: p_Code = KeyboardInputCodes::F3; return true;
        case SDL_SCANCODE_F4: p_Code = KeyboardInputCodes::F4; return true;
        case SDL_SCANCODE_F6: p_Code = KeyboardInputCodes::F6; return true;
        case SDL_SCANCODE_F8: p_Code = KeyboardInputCodes::F8; return true;

        case SDL_SCANCODE_GRAVE:  p_Code = KeyboardInputCodes::Grave; return true;
        case SDL_SCANCODE_ESCAPE: p_Code = KeyboardInputCodes::Esc;   return true;

        case SDL_SCANCODE_P: p_Code = KeyboardInputCodes::P; return true;
        case SDL_SCANCODE_Q: p_Code = KeyboardInputCodes::Q; return true;

        case SDL_SCANCODE_W: p_Code = KeyboardInputCodes::W; return true;
        case SDL_SCANCODE_A: p_Code = KeyboardInputCodes::A; return true;
        case SDL_SCANCODE_S: p_Code = KeyboardInputCodes::S; return true;
        case SDL_SCANCODE_D: p_Code = KeyboardInputCodes::D; return true;

        case SDL_SCANCODE_LSHIFT:
        case SDL_SCANCODE_RSHIFT: p_Code = KeyboardInputCodes::Shift; return true;

        case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: p_Code = KeyboardInputCodes::Control; return true;

        case SDL_SCANCODE_SPACE:     p_Code = KeyboardInputCodes::Space;     return true;
        case SDL_SCANCODE_RETURN:    p_Code = KeyboardInputCodes::Enter;     return true;
        case SDL_SCANCODE_BACKSPACE: p_Code = KeyboardInputCodes::Backspace; return true;
        case SDL_SCANCODE_TAB:       p_Code = KeyboardInputCodes::Tab;       return true;

        case SDL_SCANCODE_LALT:
        case SDL_SCANCODE_RALT: p_Code = KeyboardInputCodes::Alt; return true;

        case SDL_SCANCODE_UP:    p_Code = KeyboardInputCodes::Up;    return true;
        case SDL_SCANCODE_DOWN:  p_Code = KeyboardInputCodes::Down;  return true;
        case SDL_SCANCODE_LEFT:  p_Code = KeyboardInputCodes::Left;  return true;
        case SDL_SCANCODE_RIGHT: p_Code = KeyboardInputCodes::Right; return true;

        case SDL_SCANCODE_E: p_Code = KeyboardInputCodes::E; return true;
        case SDL_SCANCODE_R: p_Code = KeyboardInputCodes::R; return true;
        case SDL_SCANCODE_T: p_Code = KeyboardInputCodes::T; return true;
        case SDL_SCANCODE_F: p_Code = KeyboardInputCodes::F; return true;
        case SDL_SCANCODE_G: p_Code = KeyboardInputCodes::G; return true;
        case SDL_SCANCODE_V: p_Code = KeyboardInputCodes::V; return true;
        case SDL_SCANCODE_M: p_Code = KeyboardInputCodes::M; return true;
        case SDL_SCANCODE_H: p_Code = KeyboardInputCodes::H; return true;
        case SDL_SCANCODE_Z: p_Code = KeyboardInputCodes::Z; return true;
        case SDL_SCANCODE_X: p_Code = KeyboardInputCodes::X; return true;
        case SDL_SCANCODE_I: p_Code = KeyboardInputCodes::I; return true;
        case SDL_SCANCODE_Y: p_Code = KeyboardInputCodes::Y; return true;
        case SDL_SCANCODE_N: p_Code = KeyboardInputCodes::N; return true;

        default: return false;
    }
}

bool MapButton(Uint8 p_Button, MouseInputCodes& p_Code)
{
    switch (p_Button) {
        case SDL_BUTTON_LEFT:   p_Code = MouseInputCodes::Left;   return true;
        case SDL_BUTTON_MIDDLE: p_Code = MouseInputCodes::Middle; return true;
        case SDL_BUTTON_RIGHT:  p_Code = MouseInputCodes::Right;  return true;

        case SDL_BUTTON_X1: p_Code = MouseInputCodes::Side1; return true;
        case SDL_BUTTON_X2: p_Code = MouseInputCodes::Side2; return true;

        default: return false;
    }
}

void ParseKey(const SDL_Event& p_Event)
{
    KeyboardInputCodes Code;

    if (!MapKey(p_Event.key.scancode, Code)) {
        return;
    }

    if (p_Event.type == SDL_EVENT_KEY_DOWN) {
        m_CurrentKeys.SetState(Code);
    } else {
        m_CurrentKeys.ClearState(Code);
    }
}

void ParseButton(const SDL_Event& p_Event)
{
    MouseInputCodes Code;

    if (!MapButton(p_Event.button.button, Code)) {
        return;
    }

    if (p_Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        m_Mouse.ButtonsInputCodes.SetState(Code);
    } else {
        m_Mouse.ButtonsInputCodes.ClearState(Code);
    }
}

}

void ParseFrameInputs()
{
    m_PreviousKeys = m_CurrentKeys;

    m_Mouse.PreviousButtonsInputCodes = m_Mouse.ButtonsInputCodes;

    m_Mouse.DeltaX = 0.0f;
    m_Mouse.DeltaY = 0.0f;

    m_Mouse.ScrollX = 0.0f;
    m_Mouse.ScrollY = 0.0f;

    for (const SDL_Event& Event : Platform::SDL3App::GetEvents()) {
        switch (Event.type) {
            case SDL_EVENT_KEY_DOWN:
                if (!m_KeyboardCaptured) {
                    ParseKey(Event);
                }
                break;

            case SDL_EVENT_KEY_UP:
                ParseKey(Event);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (!m_MouseCaptured) {
                    ParseButton(Event);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                ParseButton(Event);
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_Mouse.PosX = Event.motion.x;
                m_Mouse.PosY = Event.motion.y;

                if (!m_MouseCaptured) {
                    m_Mouse.DeltaX += Event.motion.xrel;
                    m_Mouse.DeltaY += Event.motion.yrel;
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if (!m_MouseCaptured) {
                    m_Mouse.ScrollX += Event.wheel.x;
                    m_Mouse.ScrollY += Event.wheel.y;
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_CurrentKeys.ClearAllStates();

                m_Mouse.ButtonsInputCodes.ClearAllStates();
                break;

            default:
                break;
        }
    }
}

bool KeyIsPressed(KeyboardInputCodes p_Code)
{
    return !m_PreviousKeys.IsStateSet(p_Code) && m_CurrentKeys.IsStateSet(p_Code);
}

bool KeyIsHeld(KeyboardInputCodes p_Code)
{
    return m_PreviousKeys.IsStateSet(p_Code) && m_CurrentKeys.IsStateSet(p_Code);
}

bool KeyIsReleased(KeyboardInputCodes p_Code)
{
    return m_PreviousKeys.IsStateSet(p_Code) && !m_CurrentKeys.IsStateSet(p_Code);
}

bool KeyIsUp(KeyboardInputCodes p_Code)
{
    return !m_PreviousKeys.IsStateSet(p_Code) && !m_CurrentKeys.IsStateSet(p_Code);
}

void SetCapture(bool p_Mouse, bool p_Keyboard)
{
    m_MouseCaptured    = p_Mouse;
    m_KeyboardCaptured = p_Keyboard;
}

const Mouse& GetMouse() { return m_Mouse; }

}
