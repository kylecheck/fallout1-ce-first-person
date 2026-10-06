#ifndef FALLOUT_GAME_GAMEPAD_H_
#define FALLOUT_GAME_GAMEPAD_H_
#include <SDL.h>
namespace fallout {
void gamepad_init();
void gamepad_shutdown();
void gamepad_update(bool focused);
SDL_GameController* gamepad_controller();
void gamepad_mouse_state(int* dx, int* dy, bool* left);
bool gamepad_has_paddles();
}
#endif
