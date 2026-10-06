#ifndef FALLOUT_GAME_GAMEPAD_BINDINGS_H_
#define FALLOUT_GAME_GAMEPAD_BINDINGS_H_

#include <SDL.h>
#include <array>
#include <cstdint>
#include <vector>
#include "plib/gnw/kb.h"

namespace fallout {
// These events enter the normal game input loop, never an SDL/background callback.
constexpr int GAMEPAD_INTERACT = 6001;
constexpr int GAMEPAD_RETICLE_ACTION = 6002;
enum class GamepadContext { World, Map, Actions, NativeUI, Blocked };
struct GamepadFrame {
    std::uint32_t buttons = 0;
    Sint16 leftTrigger = 0;
    Sint16 rightTrigger = 0;
};
struct GamepadOutput {
    std::vector<int> keys;
    bool mouseLeft = false;
};
class GamepadBindings {
public:
    GamepadOutput update(const GamepadFrame& frame, GamepadContext context, Uint64 now)
    {
        std::uint32_t held = frame.buttons;
        // Triggers have hysteresis and behave as buttons, never repeated fire.
        leftTrigger_ = frame.leftTrigger >= (leftTrigger_ ? 12000 : 18000);
        rightTrigger_ = frame.rightTrigger >= (rightTrigger_ ? 12000 : 18000);
        if (leftTrigger_) held |= 1u << 28;
        if (rightTrigger_) held |= 1u << 29;
        if (!initialized_ || context != context_) {
            suppressed_ |= held;
            repeatAt_.fill(0);
        }
        initialized_ = true;
        context_ = context;
        suppressed_ &= held;
        const std::uint32_t available = held & ~suppressed_;
        const std::uint32_t pressed = available & ~previous_;
        previous_ = held;
        GamepadOutput out;
        auto down = [&](int button) { return (pressed & (1u << button)) != 0; };
        auto emit = [&](bool condition, int key) { if (condition) out.keys.push_back(key); };
        if (context == GamepadContext::Blocked) return out;
        const bool world = context == GamepadContext::World || context == GamepadContext::Map;
        if (world) {
            emit(down(SDL_CONTROLLER_BUTTON_B) || down(SDL_CONTROLLER_BUTTON_START), KEY_ESCAPE);
            emit(down(SDL_CONTROLLER_BUTTON_X), KEY_LOWERCASE_I);
            emit(down(SDL_CONTROLLER_BUTTON_Y), KEY_LOWERCASE_P);
            emit(down(SDL_CONTROLLER_BUTTON_LEFTSHOULDER), KEY_LOWERCASE_B);
            emit(down(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER), KEY_LOWERCASE_N);
            emit(down(SDL_CONTROLLER_BUTTON_BACK), context == GamepadContext::World ? KEY_F8 : KEY_F11);
            emit(down(SDL_CONTROLLER_BUTTON_RIGHTSTICK), KEY_F11);
            emit(down(28), KEY_LOWERCASE_M);
#if SDL_VERSION_ATLEAST(2, 0, 14)
            // SDL2's Deck mapping: P1=R4, P2=L4, P3=R5, P4=L5.
            emit(down(SDL_CONTROLLER_BUTTON_PADDLE1), KEY_LOWERCASE_R);
            emit(down(SDL_CONTROLLER_BUTTON_PADDLE2), context == GamepadContext::World ? KEY_F8 : KEY_F11);
            emit(down(SDL_CONTROLLER_BUTTON_PADDLE3), KEY_F11);
            emit(down(SDL_CONTROLLER_BUTTON_PADDLE4), KEY_SPACE);
#endif
            if (context == GamepadContext::World) {
                emit(down(SDL_CONTROLLER_BUTTON_A), GAMEPAD_INTERACT);
                emit(down(29), GAMEPAD_RETICLE_ACTION);
            } else {
                out.mouseLeft = (available & ((1u << SDL_CONTROLLER_BUTTON_A) | (1u << 29))) != 0;
            }
            return out;
        }
        // Native inventory/dialogue stays pointer driven: A/RT click and drag;
        // Start also supplies Enter to screens with keyboard confirmation.
        if (context == GamepadContext::NativeUI) {
            out.mouseLeft = (available & ((1u << SDL_CONTROLLER_BUTTON_A) | (1u << 29))) != 0;
            emit(down(SDL_CONTROLLER_BUTTON_B) || down(SDL_CONTROLLER_BUTTON_BACK), KEY_ESCAPE);
            emit(down(SDL_CONTROLLER_BUTTON_START), KEY_RETURN);
        } else {
            emit(down(SDL_CONTROLLER_BUTTON_A) || down(SDL_CONTROLLER_BUTTON_START), KEY_RETURN);
            emit(down(SDL_CONTROLLER_BUTTON_B) || down(SDL_CONTROLLER_BUTTON_BACK), KEY_ESCAPE);
#if SDL_VERSION_ATLEAST(2, 0, 14)
            emit(down(SDL_CONTROLLER_BUTTON_PADDLE2), KEY_ESCAPE);
#endif
        }
        constexpr int buttons[] = { SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
            SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
        constexpr int keys[] = { KEY_ARROW_UP, KEY_ARROW_DOWN, KEY_ARROW_LEFT, KEY_ARROW_RIGHT };
        for (int i = 0; i < 4; i++) {
            const bool heldDirection = (available & (1u << buttons[i])) != 0;
            if (!heldDirection) { repeatAt_[i] = 0; continue; }
            if (down(buttons[i])) {
                out.keys.push_back(keys[i]);
                repeatAt_[i] = now + 350;
            } else if (repeatAt_[i] != 0 && now >= repeatAt_[i]) {
                out.keys.push_back(keys[i]);
                repeatAt_[i] = now + 120;
            }
        }
        return out;
    }
    void reset() { *this = GamepadBindings(); }
private:
    bool initialized_ = false;
    GamepadContext context_ = GamepadContext::Blocked;
    std::uint32_t previous_ = 0, suppressed_ = 0;
    bool leftTrigger_ = false, rightTrigger_ = false;
    std::array<Uint64, 4> repeatAt_ {};
};
} // namespace fallout
#endif
