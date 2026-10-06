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
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>

namespace fallout {
static SDL_GameController* controller = nullptr;
static GamepadBindings bindings;
static std::vector<SDL_GameController*> controllers;
static SDL_JoystickID activeEvent = -1;
static bool buttonEvent = false;
static int lastKey = -1;
static char diagnostic[256] = "PAD: discovering controllers";
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
    for (SDL_GameController* pad : controllers) SDL_GameControllerClose(pad);
    controllers.clear();
    controller = nullptr;
    activeEvent = -1;
    buttonEvent = false;
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
void gamepad_note_key(int key) { lastKey = key; }
const char* gamepad_diagnostic() { return diagnostic; }
void gamepad_handle_event(const SDL_Event& event)
{
    if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED) {
        discoveryAt = 0;
        return;
    }
    SDL_JoystickID id = -1;
    bool down = false;
    if (event.type == SDL_CONTROLLERBUTTONDOWN) { id = event.cbutton.which; down = true; }
    if (event.type == SDL_CONTROLLERAXISMOTION) {
        const bool trigger = event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT
            || event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
        if (trigger ? event.caxis.value >= 18000 : std::abs(int(event.caxis.value)) >= 11000)
            id = event.caxis.which;
    }
    if (id != -1) {
        if (id != activeEvent) buttonEvent = false;
        activeEvent = id;
        buttonEvent = buttonEvent || down;
    }
}
static void refresh_diagnostic(GamepadContext current, bool focused)
{
    const char* names[] = { "WORLD", "MAP", "ACTIONS", "UI", "BLOCKED" };
    char text[256];
    if (controller == nullptr) {
        std::snprintf(text, sizeof(text), "PAD: none  KEY: %d", lastKey);
    } else {
        unsigned int buttons = 0;
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 28; i++) {
            if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(i))) buttons |= 1u << i;
        }
        std::snprintf(text, sizeof(text), "PAD: %.32s  %s%s  RX/RY: %d/%d  BTN: %x  KEY: %d  GRIPS: %s",
            SDL_GameControllerName(controller), names[static_cast<int>(current)], focused ? "" : " / UNFOCUSED",
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX) / 1024,
            SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY) / 1024,
            buttons, lastKey, gamepad_has_paddles() ? "yes" : "no");
    }
    if (std::strcmp(diagnostic, text) != 0) {
        std::snprintf(diagnostic, sizeof(diagnostic), "%s", text);
        first_person_render();
    }
}
void gamepad_update(bool focused)
{
    const Uint64 now = SDL_GetTicks64();
    for (auto it = controllers.begin(); it != controllers.end();) {
        if (!SDL_GameControllerGetAttached(*it)) {
            if (*it == controller) { controller = nullptr; bindings.reset(); }
            SDL_GameControllerClose(*it);
            it = controllers.erase(it);
            discoveryAt = 0;
        } else ++it;
    }
    if (now >= discoveryAt) {
        discoveryAt = now + 1000;
        for (int i = 0; i < SDL_NumJoysticks(); i++) {
            if (!SDL_IsGameController(i)) continue;
            const SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
            const bool known = std::any_of(controllers.begin(), controllers.end(), [&](SDL_GameController* pad) {
                return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) == id;
            });
            if (known) continue;
            SDL_GameController* candidate = SDL_GameControllerOpen(i);
            if (candidate != nullptr) controllers.push_back(candidate);
        }
    }
    if (controller == nullptr && !controllers.empty()) {
        // Preserve enumeration order; capabilities do not establish which
        // device Steam is actually delivering input through.
        controller = controllers.front();
        bindings.reset();
    }
    if (focused && activeEvent != -1) {
        for (SDL_GameController* pad : controllers) {
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad)) != activeEvent) continue;
            if (pad != controller) {
                controller = pad;
                bindings.reset();
                // A real button-down that selected a new device is intentional.
                // Prime with released input so that first press is not lost.
                if (buttonEvent) bindings.update({}, context(), now);
                mouseLeft = false;
                mouseX = mouseY = 0;
                debug_printf("Native gamepad selected by input: %s\n", SDL_GameControllerName(pad));
            }
            break;
        }
    }
    activeEvent = -1;
    buttonEvent = false;
    const double dt = ticks == 0 ? 0 : std::fmin(0.05, (now - ticks) / 1000.0);
    ticks = now;
    if (controller == nullptr || !nativeEnabled || !focused) {
        mouseLeft = false;
        mouseX = mouseY = 0;
        bindings.reset();
        refresh_diagnostic(context(), focused);
        return;
    }
    GamepadFrame frame;
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX && i < 28; i++) {
        if (SDL_GameControllerGetButton(controller, static_cast<SDL_GameControllerButton>(i))) frame.buttons |= 1u << i;
    }
    frame.leftTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT);
    frame.rightTrigger = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
    const GamepadContext current = context();
    refresh_diagnostic(current, focused);
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
