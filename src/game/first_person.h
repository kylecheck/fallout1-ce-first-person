#ifndef FALLOUT_GAME_FIRST_PERSON_H_
#define FALLOUT_GAME_FIRST_PERSON_H_

namespace fallout {

struct Object;

// Experimental first-person presentation layer. The Fallout simulation remains
// unchanged; presentation and input picking share the first-person projection.
bool first_person_is_enabled();
int first_person_window();
void first_person_toggle();
void first_person_update();
void first_person_cycle_mode();
int first_person_mode();
// Scoped native UI ownership; nesting is safe (options -> load -> map).
class FirstPersonModalScope {
public:
    FirstPersonModalScope();
    ~FirstPersonModalScope();
    FirstPersonModalScope(const FirstPersonModalScope&) = delete;
    FirstPersonModalScope& operator=(const FirstPersonModalScope&) = delete;
private:
    bool active_;
};
bool first_person_world_input_allowed();
// Presentation ownership excludes native modal UI and the action menu,
// but remains visible during enemy turns when world input is blocked.
bool first_person_overlay_visible();
void first_person_move(int rotation);
void first_person_action_menu();
bool first_person_action_menu_active();
void first_person_notify(const char* message);
void first_person_suspend_overlay();
void first_person_resume_overlay();
bool first_person_controller_move_active();
// Request a scene refresh; repeated native dirty rects coalesce until present.
void first_person_render();
void first_person_flush_render();
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
