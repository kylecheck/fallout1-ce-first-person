#ifndef FALLOUT_GAME_FIRST_PERSON_H_
#define FALLOUT_GAME_FIRST_PERSON_H_

namespace fallout {

struct Object;

// Experimental first-person presentation layer. The Fallout simulation remains
// unchanged; presentation and input picking share the first-person projection.
bool first_person_is_enabled();
void first_person_toggle();
void first_person_render();
int first_person_target_tile(int screenX, int screenY);
Object* first_person_object_at(int screenX, int screenY, int objectType, bool includeDude, int elevation);

} // namespace fallout

#endif /* FALLOUT_GAME_FIRST_PERSON_H_ */
