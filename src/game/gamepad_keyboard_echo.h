#ifndef FALLOUT_GAME_GAMEPAD_KEYBOARD_ECHO_H_
#define FALLOUT_GAME_GAMEPAD_KEYBOARD_ECHO_H_
#include <SDL.h>
#include <array>
#include <algorithm>
#include <cstdint>
namespace fallout {
// SDL2 keyboard events do not identify their physical source. Only suppress
// the observed Deck desktop keys when paired with a native button press.
class GamepadKeyboardEcho {
public:
    void reset() { pressed = {}; seen = {}; suppressed = {}; }
    void note_button(int button, Uint32 timestamp)
    {
        if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) return;
        pressed[button] = timestamp;
        seen[button] = true;
    }
    bool filter(const SDL_Event& event, bool enabled)
    {
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) return false;
        const int scan = event.key.keysym.scancode;
        if (scan < 0 || scan >= SDL_NUM_SCANCODES) return false;
        if (suppressed[scan]) {
            if (event.type == SDL_KEYUP) suppressed[scan] = false;
            return true;
        }
        if (!enabled || event.type != SDL_KEYDOWN || event.key.repeat) return false;
        const int button = echo_button(static_cast<SDL_Scancode>(scan));
        if (button < 0 || !seen[button]) return false;
        // Unsigned subtraction handles SDL's millisecond timestamp wrap.
        // Both orders are accepted because the event batch is observed first.
        const Uint32 distance = std::min(event.key.timestamp - pressed[button],
            pressed[button] - event.key.timestamp);
        if (distance > 40) return false;
        suppressed[scan] = true;
        return true;
    }
private:
    static int echo_button(SDL_Scancode scan)
    {
        switch (scan) {
        case SDL_SCANCODE_RETURN: return SDL_CONTROLLER_BUTTON_A;
        case SDL_SCANCODE_ESCAPE: return SDL_CONTROLLER_BUTTON_B;
        case SDL_SCANCODE_SPACE: return SDL_CONTROLLER_BUTTON_Y;
        case SDL_SCANCODE_TAB: return SDL_CONTROLLER_BUTTON_BACK;
        case SDL_SCANCODE_LCTRL: return SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
        case SDL_SCANCODE_LALT: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
        case SDL_SCANCODE_UP: return SDL_CONTROLLER_BUTTON_DPAD_UP;
        case SDL_SCANCODE_DOWN: return SDL_CONTROLLER_BUTTON_DPAD_DOWN;
        case SDL_SCANCODE_LEFT: return SDL_CONTROLLER_BUTTON_DPAD_LEFT;
        case SDL_SCANCODE_RIGHT: return SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
        case SDL_SCANCODE_PAGEUP: return SDL_CONTROLLER_BUTTON_PADDLE1;
        case SDL_SCANCODE_LSHIFT: return SDL_CONTROLLER_BUTTON_PADDLE2;
        case SDL_SCANCODE_PAGEDOWN: return SDL_CONTROLLER_BUTTON_PADDLE3;
        case SDL_SCANCODE_LGUI: return SDL_CONTROLLER_BUTTON_PADDLE4;
        default: return -1;
        }
    }
    std::array<Uint32, SDL_CONTROLLER_BUTTON_MAX> pressed {};
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> seen {};
    std::array<bool, SDL_NUM_SCANCODES> suppressed {};
};
}
#endif
