#ifndef FALLOUT_GAME_FIRST_PERSON_H_
#define FALLOUT_GAME_FIRST_PERSON_H_

namespace fallout {

// Experimental first-person presentation layer. The Fallout simulation remains
// unchanged; this module only replaces the contents of the isometric viewport.
bool first_person_is_enabled();
void first_person_toggle();
void first_person_render();

} // namespace fallout

#endif /* FALLOUT_GAME_FIRST_PERSON_H_ */
