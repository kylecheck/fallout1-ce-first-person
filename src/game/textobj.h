#ifndef FALLOUT_GAME_TEXTOBJ_H_
#define FALLOUT_GAME_TEXTOBJ_H_

#include "game/object_types.h"
#include "plib/gnw/rect.h"

namespace fallout {

int text_object_init(unsigned char* windowBuffer, int width, int height);
int text_object_reset();
void text_object_exit();
void text_object_disable();
void text_object_enable();
int text_object_is_enabled();
void text_object_set_base_delay(double value);
unsigned int text_object_get_base_delay();
void text_object_set_line_delay(double value);
unsigned int text_object_get_line_delay();
int text_object_create(Object* object, char* string, int font, int color, int a5, Rect* rect);
void text_object_render(Rect* rect);
int text_object_count();
void text_object_remove(Object* object);
// Read-only view of the live floating texts, for first person's speech
// bubbles: the owner (null when unbounded) and the rendered text image
// (index 0 transparent). Returns false past the end.
bool text_object_get(int index, Object** owner, const unsigned char** data, int* width, int* height,
    unsigned int* time);

} // namespace fallout

#endif /* FALLOUT_GAME_TEXTOBJ_H_ */
