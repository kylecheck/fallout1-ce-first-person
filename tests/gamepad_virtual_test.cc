#include "game/gamepad.h"
#include "game/gamepad_bindings.h"
#include "game/object.h"
#include "plib/gnw/dxinput.h"
#include <cassert>
#include <vector>
#include <iostream>
namespace fallout {
Object player {};
Object* obj_dude = &player;
int display_win = 1;
static bool fp = true, menu = false;
static int top = 5;
static std::vector<int> commands;
bool first_person_is_enabled() { return fp; }
bool first_person_world_input_allowed() { return fp && top == 5; }
bool first_person_action_menu_active() { return menu; }
int first_person_window() { return 5; }
int win_get_top_win(int, int) { return top; }
int screenGetWidth() { return 1280; }
int screenGetHeight() { return 800; }
bool intface_is_enabled() { return true; }
void GNW_add_input_buffer(int key) { commands.push_back(key); }
int debug_printf(const char*, ...) { return 0; }
}
int main()
{
    using namespace fallout;
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    gamepad_init();
    SDL_VirtualJoystickDesc desc {};
    desc.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes = SDL_CONTROLLER_AXIS_MAX;
    desc.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    desc.axis_mask = (1u << SDL_CONTROLLER_AXIS_MAX) - 1;
    desc.button_mask = (1u << SDL_CONTROLLER_BUTTON_MAX) - 1;
    const int index = SDL_JoystickAttachVirtualEx(&desc);
    assert(index >= 0);
    SDL_Joystick* joy = SDL_JoystickOpen(index);
    assert(joy != nullptr);
    auto tick = [&]() { SDL_JoystickUpdate(); gamepad_update(true); };
    tick();
    assert(gamepad_controller() != nullptr && gamepad_has_paddles());
    // SDL virtual trigger axes initialize at 0 (half travel); release first.
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
    tick(); commands.clear();
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_X, 1);
    tick();
    assert(commands.size() == 1 && commands.back() == KEY_LOWERCASE_I);
    tick(); assert(commands.size() == 1);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_X, 0); tick();
    menu = true; tick(); commands.clear();
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1); tick();
    assert(commands.back() == KEY_ARROW_DOWN);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 0); tick();
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1); tick();
    assert(commands.back() == KEY_RETURN);
    // Same held A cannot leak as a click into native UI after confirming.
    menu = false; top = 9; tick();
    MouseData mouse {};
    assert(dxinput_get_mouse_state(&mouse) && !mouse.buttons[0]);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 0); tick();
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1); tick();
    assert(dxinput_get_mouse_state(&mouse) && mouse.buttons[0]);
    tick(); assert(dxinput_get_mouse_state(&mouse) && mouse.buttons[0]);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 0); tick();
    assert(dxinput_get_mouse_state(&mouse) && !mouse.buttons[0]);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_RIGHTX, 32767);
    tick(); SDL_Delay(20); tick();
    assert(dxinput_get_mouse_state(&mouse) && mouse.x > 0);
    SDL_JoystickSetVirtualButton(joy, SDL_CONTROLLER_BUTTON_A, 1); tick();
    gamepad_update(false);
    assert(dxinput_get_mouse_state(&mouse) && !mouse.buttons[0]);
    tick(); assert(dxinput_get_mouse_state(&mouse) && !mouse.buttons[0]);
    SDL_JoystickClose(joy);
    SDL_JoystickDetachVirtual(index); tick();
    assert(gamepad_controller() == nullptr);
    assert(dxinput_get_mouse_state(&mouse) && !mouse.buttons[0]);
    gamepad_shutdown(); SDL_Quit();
    std::cout << "PASS SDL virtual gamepad commands, menu confirmation, native click/drag, pointer and disconnect\n";
}
