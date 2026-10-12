#include "game/gamepad_keyboard_echo.h"
#include <cassert>
#include <iostream>
using namespace fallout;
int main()
{
    GamepadKeyboardEcho filter;
    SDL_Event key {};
    key.type = SDL_KEYDOWN;
    key.key.keysym.scancode = SDL_SCANCODE_SPACE;
    key.key.timestamp = 100;
    assert(!filter.filter(key,true)); // No native button: normal keyboard.
    filter.note_button(SDL_CONTROLLER_BUTTON_Y,110);
    assert(filter.filter(key,true)); // Keyboard first in the observed batch.
    key.key.repeat = 1; key.key.timestamp = 900;
    assert(filter.filter(key,true));
    key.type = SDL_KEYUP;
    assert(filter.filter(key,false)); // Paired release, even if native mode changes.
    key.type = SDL_KEYDOWN; key.key.repeat = 0;
    assert(!filter.filter(key,true)); // Outside the correlation window.
    key.key.timestamp = 120;
    assert(!filter.filter(key,false)); // Opt-out.
    key.key.keysym.scancode = SDL_SCANCODE_P;
    assert(!filter.filter(key,true)); // Unrelated keyboard key.
    key.key.keysym.scancode = SDL_SCANCODE_SPACE;
    filter.reset();
    filter.note_button(SDL_CONTROLLER_BUTTON_Y,0xFFFFFFF0u);
    key.key.timestamp = 12;
    assert(filter.filter(key,true)); // SDL timestamp rollover.
    filter.reset();
    key.key.timestamp = 500;
    key.key.keysym.scancode = SDL_SCANCODE_PAGEDOWN;
    filter.note_button(SDL_CONTROLLER_BUTTON_PADDLE3,500);
    assert(filter.filter(key,true));
    filter.reset();
    assert(!filter.filter(key,true));
    std::cout << "PASS Deck keyboard echo order, pairing, timing, opt-out and rollover\n";
}
