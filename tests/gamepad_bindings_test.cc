#include "game/gamepad_bindings.h"
#include <cassert>
#include <iostream>
using namespace fallout;
static GamepadFrame button(int b) { GamepadFrame f; f.buttons = 1u << b; return f; }
int main()
{
    GamepadBindings bindings;
    Uint64 t = 1;
    const GamepadContext world = GamepadContext::World;
    bindings.update({}, world, t++);
    auto check = [&](int b, int key) {
        const auto frame = button(b);
        auto out = bindings.update(frame, world, t++);
        assert(out.keys.size() == 1 && out.keys[0] == key);
        assert(bindings.update(frame, world, t++).keys.empty());
        bindings.update({}, world, t++);
    };
    check(SDL_CONTROLLER_BUTTON_A, GAMEPAD_INTERACT);
    check(SDL_CONTROLLER_BUTTON_B, KEY_ESCAPE);
    check(SDL_CONTROLLER_BUTTON_X, KEY_LOWERCASE_I);
    check(SDL_CONTROLLER_BUTTON_Y, KEY_LOWERCASE_P);
    check(SDL_CONTROLLER_BUTTON_LEFTSHOULDER, KEY_LOWERCASE_B);
    check(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, KEY_LOWERCASE_N);
    check(SDL_CONTROLLER_BUTTON_BACK, KEY_F8);
    check(SDL_CONTROLLER_BUTTON_RIGHTSTICK, KEY_F11);
#if SDL_VERSION_ATLEAST(2, 0, 14)
    check(SDL_CONTROLLER_BUTTON_PADDLE1, KEY_LOWERCASE_R);
    check(SDL_CONTROLLER_BUTTON_PADDLE2, KEY_F8);
    check(SDL_CONTROLLER_BUTTON_PADDLE3, KEY_F11);
    check(SDL_CONTROLLER_BUTTON_PADDLE4, KEY_SPACE);
#endif
    GamepadFrame left;
    left.leftTrigger = 19000;
    assert(bindings.update(left, world, t++).keys[0] == KEY_LOWERCASE_M);
    assert(bindings.update(left, world, t++).keys.empty());
    bindings.update({}, world, t++);
    GamepadFrame trigger;
    trigger.rightTrigger = 19000;
    assert(bindings.update(trigger, world, t++).keys[0] == GAMEPAD_RETICLE_ACTION);
    trigger.rightTrigger = 14000;
    assert(bindings.update(trigger, world, t++).keys.empty());
    trigger.rightTrigger = 11999;
    bindings.update(trigger, world, t++);
    trigger.rightTrigger = 17000;
    assert(bindings.update(trigger, world, t++).keys.empty());
    trigger.rightTrigger = 19000;
    assert(bindings.update(trigger, world, t++).keys[0] == GAMEPAD_RETICLE_ACTION);
    // Held trigger never clicks an inventory that appears after an attack.
    assert(!bindings.update(trigger, GamepadContext::NativeUI, t++).mouseLeft);
    bindings.update({}, GamepadContext::NativeUI, t++);
    assert(bindings.update(trigger, GamepadContext::NativeUI, t++).mouseLeft);
    assert(bindings.update(trigger, GamepadContext::NativeUI, t++).mouseLeft);
    assert(!bindings.update({}, GamepadContext::NativeUI, t++).mouseLeft);
    const GamepadContext menu = GamepadContext::Actions;
    bindings.update({}, menu, 1000);
    auto down = button(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    assert(bindings.update(down, menu, 1001).keys[0] == KEY_ARROW_DOWN);
    assert(bindings.update(down, menu, 1350).keys.empty());
    assert(bindings.update(down, menu, 1351).keys[0] == KEY_ARROW_DOWN);
    assert(bindings.update(down, menu, 1470).keys.empty());
    assert(bindings.update(down, menu, 1471).keys[0] == KEY_ARROW_DOWN);
    bindings.update({}, menu, 1472);
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_A), menu, 1473).keys[0] == KEY_RETURN);
    assert(!bindings.update(button(SDL_CONTROLLER_BUTTON_A), GamepadContext::NativeUI, 1474).mouseLeft);
    bindings.update({}, GamepadContext::NativeUI, 1475);
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_A), GamepadContext::NativeUI, 1476).mouseLeft);
    // Disable/reconnect/focus regain never executes already-held buttons.
    bindings.reset();
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_A), world, 1500).keys.empty());
    bindings.update({}, world, 1501);
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_A), world, 1502).keys[0] == GAMEPAD_INTERACT);
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_X), GamepadContext::Blocked, 1503).keys.empty());
    assert(bindings.update(button(SDL_CONTROLLER_BUTTON_X), world, 1504).keys.empty());
    std::cout << "PASS gamepad layout, trigger hysteresis, menu repeat and held-input suppression\n";
}
