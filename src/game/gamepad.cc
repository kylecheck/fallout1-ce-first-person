#include "game/gamepad.h"
#include "game/gamepad_bindings.h"
#include "game/gamepad_keyboard_echo.h"
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
#include <cstdarg>
#include <unordered_map>

namespace fallout {
static SDL_GameController* controller = nullptr;
static GamepadBindings bindings;
static std::vector<SDL_GameController*> controllers;
static SDL_JoystickID activeEvent = -1;
static bool buttonEvent = false;
static int lastKey = -1;
static char diagnostic[256] = "PAD: discovering controllers";
static bool nativeEnabled = true;
static bool echoEnabled = true;
static GamepadKeyboardEcho keyboardEcho;
static bool mouseLeft = false;
static double mouseX = 0, mouseY = 0;
static Uint64 ticks = 0, discoveryAt = 0;
static FILE* inputLog = nullptr;
static Uint64 logStarted = 0;
static char loggedState[256] = {};
struct LoggedAxis { Uint64 time = 0; int value = 0; bool seen = false; };
static std::unordered_map<Uint64, LoggedAxis> loggedAxes;

static const char* context_name(GamepadContext current)
{
    const char* names[] = { "WORLD", "MAP", "ACTIONS", "UI", "BLOCKED" };
    return names[static_cast<int>(current)];
}
static const char* key_name(int key)
{
    switch (key) {
    case KEY_SPACE: return "SPACE / END_TURN";
    case KEY_RETURN: return "ENTER / CONFIRM";
    case KEY_ESCAPE: return "ESCAPE / BACK_OPTIONS";
    case KEY_F8: return "F8 / ACTION_MENU";
    case KEY_F11: return "F11 / FP_TOGGLE";
    case KEY_LOWERCASE_I: return "I / INVENTORY";
    case KEY_LOWERCASE_P: return "P / PIPBOY";
    case KEY_LOWERCASE_B: return "B / SWITCH_HAND";
    case KEY_LOWERCASE_N: return "N / ATTACK_TYPE";
    case KEY_LOWERCASE_M: return "M / MODE_CYCLE";
    case KEY_LOWERCASE_R: return "R / RELOAD";
    case KEY_ARROW_UP: return "UP";
    case KEY_ARROW_DOWN: return "DOWN";
    case KEY_ARROW_LEFT: return "LEFT";
    case KEY_ARROW_RIGHT: return "RIGHT";
    case GAMEPAD_INTERACT: return "INTERACT";
    case GAMEPAD_RETICLE_ACTION: return "RETICLE_ACTION";
    default: return "OTHER";
    }
}
static void log_input(const char* format, ...)
{
    if (inputLog == nullptr) return;
    std::fprintf(inputLog, "[%llu ms] ", static_cast<unsigned long long>(SDL_GetTicks64() - logStarted));
    va_list args;
    va_start(args, format);
    std::vfprintf(inputLog, format, args);
    va_end(args);
    std::fputc('\n', inputLog);
}
static void log_axis(bool raw, SDL_JoystickID device, int axis, int value)
{
    if (inputLog == nullptr) return;
    const Uint64 key = (Uint64(Uint32(device)) << 16) | (raw ? 0x100u : 0u) | unsigned(axis);
    LoggedAxis& previous = loggedAxes[key];
    const Uint64 now = SDL_GetTicks64();
    const bool centerChanged = (std::abs(previous.value) <= 6000) != (std::abs(value) <= 6000);
    if (previous.seen && !centerChanged && now - previous.time < 100) return;
    if (previous.seen && previous.value == value) return;
    log_input("%s_AXIS device=%d axis=%d value=%d", raw ? "RAW" : "CONTROLLER", int(device), axis, value);
    previous = { now, value, true };
}
static void log_state(GamepadContext current, bool focused)
{
    char state[256];
    const int id = controller == nullptr ? -1 : SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    std::snprintf(state, sizeof(state), "context=%s focus=%d native=%d device=%d name=%.64s grips=%d top_window=%d fp=%d overlay=%d",
        context_name(current), focused, nativeEnabled, id,
        controller == nullptr ? "none" : SDL_GameControllerName(controller), gamepad_has_paddles(),
        win_get_top_visible_win(screenGetWidth() / 2, screenGetHeight() / 2),
        first_person_is_enabled(), first_person_overlay_visible());
    if (std::strcmp(loggedState, state) != 0) {
        std::snprintf(loggedState, sizeof(loggedState), "%s", state);
        log_input("STATE %s", state);
    }
}

void gamepad_init()
{
    const char* setting = std::getenv("FALLOUT_FP_GAMEPAD");
    nativeEnabled = setting == nullptr || setting[0] != '0';
    const char* echoSetting = std::getenv("FALLOUT_FP_KEYBOARD_ECHO");
    echoEnabled = echoSetting == nullptr || echoSetting[0] != '0';
    keyboardEcho.reset();
    SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    if (inputLog != nullptr) std::fclose(inputLog);
    inputLog = nullptr;
    logStarted = SDL_GetTicks64();
    loggedState[0] = '\0';
    loggedAxes.clear();
    const char* path = std::getenv("FALLOUT_FP_INPUT_LOG");
    if (path != nullptr && path[0] != '\0') {
        inputLog = std::fopen(path, "w");
        if (inputLog != nullptr) {
            std::setvbuf(inputLog, nullptr, _IOLBF, 0);
            SDL_version version;
            SDL_GetVersion(&version);
            log_input("SESSION_START native-gamepad-log-v1 build=%s %s SDL=%d.%d.%d", __DATE__, __TIME__,
                version.major, version.minor, version.patch);
            log_input("CONFIG native=%d keyboard_echo_filter=%d", nativeEnabled, echoEnabled);
            for (const char* variable : { "SteamAppId", "SteamGameId", "SteamOverlayGameId" }) {
                const char* value = std::getenv(variable);
                log_input("LAUNCH %s=%s", variable, value == nullptr ? "unset" : value);
            }
            for (int i = 0; i < SDL_NumJoysticks(); i++) {
                log_input("DISCOVER index=%d device=%d name=%s mapped=%d", i,
                    int(SDL_JoystickGetDeviceInstanceID(i)), SDL_JoystickNameForIndex(i) == nullptr ? "unknown" : SDL_JoystickNameForIndex(i), SDL_IsGameController(i));
            }
            debug_printf("Input recording enabled: %s\n", path);
        } else {
            debug_printf("Unable to open input recording: %s\n", path);
        }
    }
    bindings.reset();
    ticks = discoveryAt = 0;
}
void gamepad_shutdown()
{
    keyboardEcho.reset();
    log_input("SESSION_END");
    if (inputLog != nullptr) std::fclose(inputLog);
    inputLog = nullptr;
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
    const int top = win_get_top_visible_win(screenGetWidth() / 2, screenGetHeight() / 2);
    if (first_person_is_enabled() && top == first_person_window()) {
        return first_person_world_input_allowed() ? GamepadContext::World : GamepadContext::Blocked;
    }
    if (!first_person_is_enabled() && obj_dude != nullptr && intface_is_enabled() && top == display_win)
        return GamepadContext::Map;
    return GamepadContext::NativeUI;
}
void gamepad_note_key(int key)
{
    lastKey = key;
    log_input("KEYBOARD_DECODED code=%d name=%s", key, key_name(key));
}
const char* gamepad_diagnostic() { return diagnostic; }
void gamepad_handle_event(const SDL_Event& event)
{
    if (nativeEnabled && echoEnabled && event.type == SDL_CONTROLLERBUTTONDOWN) {
        SDL_GameController* pad = SDL_GameControllerFromInstanceID(event.cbutton.which);
        const char* name = pad == nullptr ? nullptr : SDL_GameControllerName(pad);
        if (name != nullptr && std::strstr(name, "Steam Deck") != nullptr) {
            keyboardEcho.note_button(event.cbutton.button, event.cbutton.timestamp);
        }
    }
    if (inputLog != nullptr) {
        switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            log_input("KEYBOARD_%s scancode=%d name=%s repeat=%d", event.type == SDL_KEYDOWN ? "DOWN" : "UP",
                int(event.key.keysym.scancode), SDL_GetScancodeName(event.key.keysym.scancode), event.key.repeat);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            log_input("MOUSE_%s button=%d", event.type == SDL_MOUSEBUTTONDOWN ? "DOWN" : "UP", event.button.button);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP: {
            const char* name = SDL_GameControllerGetStringForButton(static_cast<SDL_GameControllerButton>(event.cbutton.button));
            log_input("CONTROLLER_%s device=%d button=%d name=%s",
                event.type == SDL_CONTROLLERBUTTONDOWN ? "DOWN" : "UP", int(event.cbutton.which),
                event.cbutton.button, name == nullptr ? "unknown" : name);
            break;
        }
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
            log_input("RAW_%s device=%d button=%d", event.type == SDL_JOYBUTTONDOWN ? "DOWN" : "UP",
                int(event.jbutton.which), event.jbutton.button);
            break;
        case SDL_CONTROLLERAXISMOTION:
            log_axis(false, event.caxis.which, event.caxis.axis, event.caxis.value);
            break;
        case SDL_JOYAXISMOTION:
            log_axis(true, event.jaxis.which, event.jaxis.axis, event.jaxis.value);
            break;
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
            log_input("CONTROLLER_%s id_or_index=%d", event.type == SDL_CONTROLLERDEVICEADDED ? "ADDED" : "REMOVED", int(event.cdevice.which));
            break;
        default: break;
        }
    }
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
bool gamepad_filter_keyboard_echo(const SDL_Event& event)
{
    const bool filtered = keyboardEcho.filter(event, nativeEnabled && echoEnabled);
    if (filtered) log_input("KEYBOARD_ECHO_SUPPRESSED type=%s scancode=%d name=%s",
        event.type == SDL_KEYDOWN ? "DOWN" : "UP", int(event.key.keysym.scancode),
        SDL_GetScancodeName(event.key.keysym.scancode));
    return filtered;
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
            log_input("DISCONNECT device=%d name=%s", int(SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it))), SDL_GameControllerName(*it));
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
            if (candidate != nullptr) {
                controllers.push_back(candidate);
                char* mapping = SDL_GameControllerMapping(candidate);
                log_input("OPEN device=%d name=%s mapping=%s", int(id), SDL_GameControllerName(candidate),
                    mapping == nullptr ? "none" : mapping);
                SDL_free(mapping);
            }
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
                if (mouseLeft) log_input("EMULATED_MOUSE_UP reason=device-change");
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
        if (mouseLeft) log_input("EMULATED_MOUSE_UP reason=inactive");
        mouseLeft = false;
        mouseX = mouseY = 0;
        bindings.reset();
        log_state(context(), focused);
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
    log_state(current, focused);
    refresh_diagnostic(current, focused);
    const GamepadOutput out = bindings.update(frame, current, now);
    if (mouseLeft != out.mouseLeft) log_input("EMULATED_MOUSE_%s", out.mouseLeft ? "DOWN" : "UP");
    mouseLeft = out.mouseLeft;
    for (int key : out.keys) {
        log_input("ACTION source=gamepad code=%d name=%s context=%s", key, key_name(key), context_name(current));
        GNW_add_input_buffer(key);
    }
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
