#ifndef FALLOUT_GAME_FIRST_PERSON_H_
#define FALLOUT_GAME_FIRST_PERSON_H_

namespace fallout {

struct Object;

// Experimental first-person presentation layer. The Fallout simulation remains
// unchanged; presentation and input picking share the first-person projection.
bool first_person_is_enabled();
int first_person_window();
void first_person_toggle();
void first_person_render();
// Camera heading is independent of native character facing during pathing.
// Heading uses 24 steps around the circle (15 degrees each); rotation returns
// the nearest native Fallout hex direction for movement/pathing.
int first_person_heading();
int first_person_rotation();
void first_person_turn(int steps);
int first_person_target_tile(int screenX, int screenY);
Object* first_person_object_at(int screenX, int screenY, int objectType, bool includeDude, int elevation);

} // namespace fallout

#endif /* FALLOUT_GAME_FIRST_PERSON_H_ */
