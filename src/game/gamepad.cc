#include "game/gamepad.h"
#include "game/gamepad_bindings.h"
#include "game/first_person.h"
#include "game/intface.h"
#include "game/object.h"
#include "game/map.h"
#include "plib/gnw/input.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/debug.h"
#include <cmath>
#include <cstdlib>

namespace fallout {
static SDL_GameController* controller = nullptr;
static GamepadBindings bindings;
static bool nativeEnabled = true;
static bool mouseLeft = false;
static double mouseX = 0, mouseY = 0;
static Uint64 ticks = 0, discoveryAt = 0;

void gamepad_init()
{
    const char* setting = std::getenv("FALLOUT_FP_GAMEPAD");
    nativeEnabled = setting == nullptr || setting[0] != '0';
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    bindings.reset();
    ticks = discoveryAt = 0;
}
void gamepad_shutdown()
{
    if (controller != nullptr) SDL_GameControllerClose(controller);
    controller = nullptr;
    mouseLeft = false;
    mouseX = mouseY = 0;
    bindings.reset();
}
SDL_GameController* gamepad_controller() { return controller; }
bool gamepad_has_paddles()
{
#if SDL_VERSION_ATLEAST(2, 0, 14)
    return controller != nullptr && SDL_GameControllerHasButton(controller, SDL_CONTROLLER_BUTTON_PADDLE2);
#else
    return false;
#endif
}
void gamepad_mouse_state(int* dx, int* dy, bool* left)
{
    *dx = static_cast<int>(mouseX);
    *dy = static_cast<int>(mouseY);
    mouseX -= *dx;
    mouseY -= *dy;
    *left = mouseLeft;
}
static GamepadContext context()
{
    if (first_person_action_menu_active()) return GamepadContext::Actions;
    const int top = win_get_top_win(screenGetWidth() / 2, screenGetHeight() / 2);
    if (first_person_is_enabled() && top == first_person_window()) {
        return first_person_world_input_allowed() ? GamepadContext::World : GamepadContext::Blocked;
    }
    if (!first_person_is_enabled() && obj_dude != nullptr && intface_is_enabled() && top == display_win)
        return GamepadContext::Map;
    return GamepadContext::NativeUI;
}
void gamepad_update(bool focused)
{
    const Uint64 now = SDL_GetTicks64();
    if (controller != nullptr && !SDL_GameControllerGetAttached(controller)) {
        gamepad_shutdown();
        discoveryAt = 0;
    }
    if (controller == nullptr && now >= discoveryAt) {
        discoveryAt = now + 1000;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (!SDL_IsGameController(i)) continue;
            SDL_GameController* candidate = SDL_GameControllerOpen(i);
            if (candidate == nullptr) continue;
#if SDL_VERSION_ATLEAST(2, 0, 14)
            const bool candidatePaddles = SDL_GameControllerHasButton(candidate, SDL_CONTROLLER_BUTTON_PADDLE2);
#else
            const bool candidatePaddles = false;
#endif
            if (controller == nullptr || candidatePaddles) {
                if (controller != nullptr) SDL_GameControllerClose(controller);
                controller = candidate;
                if (candidatePaddles) break;
            } else {
                SDL_GameControllerClose(candidate);
            }
        }
        if (controller != nullptr) {
            debug_printf("Native gamepad: %s; rear buttons %s\n", SDL_GameControllerName(controller),
                gamepad_has_paddles() ? "available" : "hidden (use View menu / R3 view toggle)");
            bindings.reset();
        }
    }
    const double dt = ticks == 0 ? 0 : std::fmin(0.05, (now - ticks) / 1000.0);
    ticks = now;
    if (controller == nullptr || !nativeEnabled || !focused) {
        mouseLeft = false;
        mouseX = mouseY = 0;
        bindings.reset();
        return;
    }
    GamepadFrame frame;
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 28; i++) {
        if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(i))) frame.buttons |= 1u << i;
    }
    frame.leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    frame.rightTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    const GamepadContext current = context();
    const GamepadOutput out = bindings.update(frame, current, now);
    mouseLeft = out.mouseLeft;
    for (int key : out.keys) GNW_add_input_buffer(key);
    if (current == GamepadContext::NativeUI || current == GamepadContext::Map) {
        auto axis = [](Sint16 value) {
            double v = value >= 0 ? value / 32767.0 : value / 32768.0;
            return std::abs(v) <= 0.18 ? 0.0 : std::copysign((std::abs(v) - 0.18) / 0.82, v);
        };
        mouseX += axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX)) * dt * 600;
        mouseY += axis(SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY)) * dt * 600;
    } else {
        mouseX = mouseY = 0;
    }
}
} // namespace fallout
