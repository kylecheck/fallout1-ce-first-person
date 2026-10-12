#include "game/first_person.h"
#include "game/gamepad.h"
#include "game/first_person_gpu.h"
#include "game/first_person_heap_check.h"
#include "game/first_person_hud.h"
#include "game/first_person_world_gpu.h"
#include "game/first_person_projection.h"
#include "game/first_person_frame.h"
#include "game/first_person_wall.h"
#include "game/first_person_ceiling.h"
#include "game/first_person_doorway.h"
#include "game/first_person_facing.h"
#include "game/first_person_material.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include <SDL.h>

#include "game/game.h"
#include "plib/gnw/input.h"
#include "plib/gnw/kb.h"
#include "plib/gnw/button.h"
#include "game/art.h"
#include "game/anim.h"
#include "game/combat.h"
#include "game/critter.h"
#include "game/display.h"
#include "game/gmouse.h"
#include "game/intface.h"
#include "game/inventry.h"
#include "game/item.h"
#include "game/map.h"
#include "game/object_types.h"
#include "game/tile.h"
#include "game/object.h"
#include "game/perk.h"
#include "game/proto.h"
#include "game/protinst.h"
#include "game/scripts.h"
#include "game/stat.h"
#include "plib/color/color.h"
#include "plib/gnw/gnw.h"
#include "plib/gnw/grbuf.h"
#include "plib/gnw/mouse.h"
#include "plib/gnw/svga.h"
#include "plib/gnw/text.h"

namespace fallout {

enum class FirstPersonWeaponPose {
    Lowered,
    Ready,
    Attack,
    Reload,
};

struct FirstPersonWeaponProfile {
    double loweredWidth;
    double readyWidth;
    double attackWidth;
    double reloadWidth;
    double horizontalCenter;
    double loweredBottom;
    double readyBottom;
    double attackBottom;
    double reloadBottom;
};

static FirstPersonWeaponProfile first_person_weapon_profile(int weaponAnimationCode)
{
    FirstPersonWeaponProfile profile {
        0.34, 0.40, 0.43, 0.38,
        0.68,
        1.04, 0.945, 0.90, 0.98,
    };

    switch (weaponAnimationCode) {
    case WEAPON_ANIMATION_PISTOL:
        profile.horizontalCenter = 0.70;
        profile.loweredWidth = 0.30;
        profile.readyWidth = 0.36;
        profile.attackWidth = 0.39;
        break;
    case WEAPON_ANIMATION_SMG:
        profile.horizontalCenter = 0.68;
        profile.loweredWidth = 0.34;
        profile.readyWidth = 0.40;
        profile.attackWidth = 0.44;
        break;
    case WEAPON_ANIMATION_SHOTGUN:
    case WEAPON_ANIMATION_LASER_RIFLE:
        profile.horizontalCenter = 0.64;
        profile.loweredWidth = 0.40;
        profile.readyWidth = 0.46;
        profile.attackWidth = 0.50;
        break;
    case WEAPON_ANIMATION_MINIGUN:
    case WEAPON_ANIMATION_LAUNCHER:
        profile.horizontalCenter = 0.60;
        profile.loweredWidth = 0.46;
        profile.readyWidth = 0.52;
        profile.attackWidth = 0.56;
        break;
    case WEAPON_ANIMATION_KNIFE:
    case WEAPON_ANIMATION_CLUB:
    case WEAPON_ANIMATION_HAMMER:
    case WEAPON_ANIMATION_SPEAR:
        profile.horizontalCenter = 0.72;
        profile.loweredWidth = 0.28;
        profile.readyWidth = 0.34;
        profile.attackWidth = 0.40;
        break;
    default:
        break;
    }

    profile.reloadWidth = profile.readyWidth * 0.94;
    return profile;
}

static FirstPersonWeaponPose first_person_weapon_pose(int hitMode)
{
    if (hitMode == HIT_MODE_LEFT_WEAPON_RELOAD
        || hitMode == HIT_MODE_RIGHT_WEAPON_RELOAD) {
        return FirstPersonWeaponPose::Reload;
    }

    if (obj_dude != nullptr) {
        const int animation = FID_ANIM_TYPE(obj_dude->fid);
        if (animation == ANIM_PARRY_ANIM
            || animation == ANIM_THRUST_ANIM
            || animation == ANIM_SWING_ANIM
            || animation == ANIM_POINT
            || animation == ANIM_FIRE_SINGLE
            || animation == ANIM_FIRE_BURST
            || animation == ANIM_FIRE_CONTINUOUS
            || animation == ANIM_THROW_PUNCH
            || animation == ANIM_KICK_LEG
            || animation == ANIM_THROW_ANIM) {
            return FirstPersonWeaponPose::Attack;
        }
    }

    if (first_person_mode() == GAME_MOUSE_MODE_CROSSHAIR) {
        return FirstPersonWeaponPose::Ready;
    }

    return FirstPersonWeaponPose::Lowered;
}

static bool gFirstPersonEnabled = false;
static FirstPersonFrameRequest gFirstPersonScene;
static bool gFirstPersonOverlaySuspended = false;
static int gFirstPersonModalDepth = 0;
// Path of the last world frame, for heap-check labels.
static bool gFirstPersonLastFrameGpu = false;
static bool gFirstPersonActionMenuActive = false;
static char gFirstPersonNotice[512] = {};
static Uint64 gFirstPersonNoticeUntil = 0;
static int gFirstPersonMode = GAME_MOUSE_MODE_MOVE;
static constexpr double kFirstPersonEyeHeight = 0.74;
static int gFirstPersonWindow = -1;
static bool gFirstPersonRestoreInterface = false;
// Camera heading is measured in 15-degree units, but stored continuously so
// controller look can move smoothly between the 24 keyboard/tap headings.
// Native Fallout movement still resolves to the nearest one of its 6 hex
// directions.
static double gFirstPersonHeading = 0.0;
static double gFirstPersonPitchDegrees = 0.0;
static int gFirstPersonCameraRevision = 0;
static SDL_GameController* gFirstPersonController = nullptr;
static Uint64 gFirstPersonControllerTicks = 0;
static Uint64 gFirstPersonMoveTicks = 0;

// Pick IDs are written only when a visible scene pixel wins the depth test.
// Resolve against live map objects before returning; never dereference cached
// object pointers after a script might have deleted an object.
struct FirstPersonPick {
    Object* object;
    int id;
};
static std::vector<FirstPersonPick> gFirstPersonPicks;
// Interaction proxy picks cover the opaque source bounds of billboard objects,
// not just their currently opaque destination pixels. This keeps large nearby
// terminals/containers selectable even when the cursor sits over a scaled-up
// transparent hole in the original isometric FRM.
static std::vector<FirstPersonPick> gFirstPersonInteractionPicks;
static int gPickWidth = 0;
static int gPickHeight = 0;
static int gPickTile = -1;
static int gPickRotation = -1; // camera revision for pick-buffer validity
static int gPickElevation = -1;

// Structural inputs are collected before any camera-space clipping.
struct FirstPersonWallSprite {
    Object* object;
    int fid;
    int direction;
    int tile;
    unsigned int extendedFlags;
    double worldX;
    double worldY;
    double baseHeight = 0.0;
    double materialVMax = 1.0;
    double materialUMin = 0.0;
    double materialUMax = 1.0;
};

struct FirstPersonWallMaterial {
    int fid;
    int direction;
    int width;
    int height;
    std::vector<unsigned char> pixels;
};
static std::vector<FirstPersonWallMaterial> gFirstPersonWallMaterials;

static std::vector<double> gFirstPersonDepthBuffer;
static std::vector<double> gFirstPersonInteractionDepth;

struct FirstPersonRenderedBounds {
    Object* object;
    int id;
    int left;
    int top;
    int right;
    int bottom;
};

struct FirstPersonDoorSprite {
    Object* object;
    int fid;
    int frame;
    int direction;
    int tile;
    unsigned int extendedFlags;
    double worldX;
    double worldY;
    bool portal = false;
    bool damagedPanel = false;
};

struct FirstPersonObjectSprite {
    Object* object;
    int fid;
    int frame;
    int direction;
    int type;
    double x;
    double z;
};

// Converts an isometric screen-pixel offset on the ground plane (a native
// object's sub-tile x/y shift) into a renderer world-plane displacement; the
// inverse of the floor mapping's basis.
static void first_person_iso_to_world(double isoX, double isoY, double* worldX, double* worldY)
{
    constexpr double kIsoXFromWorldX = 27.712812921102035;
    constexpr double kIsoYFromWorldX = -6.928203230275509;
    constexpr double kDeterminant = kIsoXFromWorldX * 12.0 - 16.0 * kIsoYFromWorldX;
    *worldX = (12.0 * isoX - 16.0 * isoY) / kDeterminant;
    *worldY = (-kIsoYFromWorldX * isoX + kIsoXFromWorldX * isoY) / kDeterminant;
}

struct FirstPersonFloorArt {
    int fid;
    Art* art;
    ArtFrame* frame;
    unsigned char* pixels;
    CacheEntry* cacheEntry;
};

bool first_person_is_enabled()
{
    return gFirstPersonEnabled;
}

int first_person_window()
{
    if (gFirstPersonEnabled && gFirstPersonWindow != -1) {
        return gFirstPersonWindow;
    }

    return display_win;
}

void first_person_toggle()
{
    first_person_heap_check("fp-toggle-before");
    gFirstPersonEnabled = !gFirstPersonEnabled;
    gFirstPersonScene.request();
    gFirstPersonOverlaySuspended = false;
    gFirstPersonModalDepth = 0;

    if (gFirstPersonEnabled) {
        if (gFirstPersonWindow == -1) {
            gFirstPersonWindow = win_add(
                0,
                0,
                screenGetWidth(),
                screenGetHeight(),
                colorTable[0],
                WINDOW_HIDDEN | WINDOW_MOVE_ON_TOP);
        }

        gFirstPersonRestoreInterface = !intface_is_hidden();
        if (gFirstPersonRestoreInterface) {
            intface_hide();
        }

        if (gFirstPersonWindow != -1) {
            win_show(gFirstPersonWindow);
        }

        gFirstPersonMode = GAME_MOUSE_MODE_MOVE;

        if (obj_dude != nullptr) {
            const int nativeRotation = ((obj_dude->rotation % ROTATION_COUNT)
                + ROTATION_COUNT) % ROTATION_COUNT;
            gFirstPersonHeading = nativeRotation * 4.0;
            gFirstPersonPitchDegrees = 0.0;
            gFirstPersonCameraRevision++;
        }
    } else {
        if (gFirstPersonWindow != -1) {
            win_hide(gFirstPersonWindow);
        }

        if (gFirstPersonRestoreInterface) {
            intface_show();
        }
        gFirstPersonRestoreInterface = false;
    }

    gFirstPersonPicks.clear();
    gFirstPersonInteractionPicks.clear();
    first_person_heap_check("fp-toggle-after");
}

// The view heading belongs to presentation, not native animation/pathing.
// Native movement remains free to rotate the character at every hex step.
int first_person_heading()
{
    constexpr int kHeadingCount = ROTATION_COUNT * 4;
    int heading = static_cast<int>(std::lround(gFirstPersonHeading));
    return ((heading % kHeadingCount) + kHeadingCount) % kHeadingCount;
}

static bool first_person_update_controller_look();
static void first_person_update_controller_move();

int first_person_rotation()
{
    // Round the continuous presentation heading to the nearest native
    // 60-degree hex direction. Fallout pathing/collision remains authoritative
    // even while the view freely looks between those axes.
    int rotation = static_cast<int>(std::lround(gFirstPersonHeading / 4.0));
    return ((rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
}

void first_person_turn(int steps)
{
    if (!gFirstPersonEnabled) {
        return;
    }

    constexpr double kHeadingCount = ROTATION_COUNT * 4.0;
    gFirstPersonHeading = std::fmod(gFirstPersonHeading + steps, kHeadingCount);
    if (gFirstPersonHeading < 0.0) {
        gFirstPersonHeading += kHeadingCount;
    }

    gFirstPersonCameraRevision++;
    gFirstPersonPicks.clear();
    gFirstPersonInteractionPicks.clear();
}

bool first_person_controller_move_active()
{
    SDL_GameController* pad = gamepad_controller();
    if (!gFirstPersonEnabled || pad == nullptr) {
        return false;
    }

    const Sint16 rawX = SDL_GameControllerGetAxis(
        pad,
        SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 rawY = SDL_GameControllerGetAxis(
        pad,
        SDL_CONTROLLER_AXIS_LEFTY);

    const double x = rawX >= 0 ? rawX / 32767.0 : rawX / 32768.0;
    const double y = rawY >= 0 ? rawY / 32767.0 : rawY / 32768.0;
    constexpr double kMoveDeadZone = 0.32;
    return std::sqrt(x * x + y * y) >= kMoveDeadZone;
}

void first_person_cycle_mode()
{
    if (!gFirstPersonEnabled) {
        gmouse_3d_toggle_mode();
        return;
    }

    switch (gFirstPersonMode) {
    case GAME_MOUSE_MODE_MOVE:
        gFirstPersonMode = GAME_MOUSE_MODE_ARROW;
        break;
    case GAME_MOUSE_MODE_ARROW:
        gFirstPersonMode = GAME_MOUSE_MODE_CROSSHAIR;
        break;
    case GAME_MOUSE_MODE_CROSSHAIR:
    default:
        gFirstPersonMode = GAME_MOUSE_MODE_MOVE;
        break;
    }

    // Do not ask Fallout's legacy mouse-mode machine to represent the
    // first-person state. Outside combat it actively rejects CROSSHAIR and
    // rewrites modes based on the hidden map cursor. First person owns this
    // three-state cycle and routes clicks itself.
    if (gFirstPersonMode == GAME_MOUSE_MODE_ARROW) {
        gmouse_3d_set_mode(GAME_MOUSE_MODE_ARROW);
    } else {
        gmouse_3d_set_mode(GAME_MOUSE_MODE_MOVE);
    }

    first_person_render();
}

int first_person_mode()
{
    // Native inventory/skill commands own their temporary targeting modes.
    const int nativeMode = gmouse_3d_get_mode();
    if (!gFirstPersonEnabled || nativeMode >= GAME_MOUSE_MODE_CROSSHAIR) return nativeMode;
    return gFirstPersonMode;
}

void first_person_suspend_overlay()
{
    if (gFirstPersonEnabled && gFirstPersonWindow != -1) {
        if (gFirstPersonModalDepth++ == 0) {
            gFirstPersonOverlaySuspended = true;
            win_hide(gFirstPersonWindow);
        }
    }
}

void first_person_resume_overlay()
{
    if (gFirstPersonEnabled && gFirstPersonWindow != -1) {
        if (gFirstPersonModalDepth == 0 || --gFirstPersonModalDepth != 0) return;
        gFirstPersonOverlaySuspended = false;
        gFirstPersonPicks.clear();
        gFirstPersonInteractionPicks.clear();
        gFirstPersonControllerTicks = 0;
        gFirstPersonMoveTicks = 0;
        intface_hide();
        win_show(gFirstPersonWindow);
        first_person_render();
    }
}

bool first_person_action_menu_active() { return gFirstPersonActionMenuActive; }

// Command code for the action menu's stimpak entry, handled here rather than
// through the input buffer.
static constexpr int kFirstPersonUseStimpak = -1000;

// Uses a carried stimpak as the inventory would: in combat it costs the AP
// the inventory charges for opening (4, less Quick Pockets), and healing goes
// through native item_d_take_drug. Removal follows the AI's drug use.
static void first_person_use_stimpak()
{
    static char noStimpak[] = "You have no stimpaks.";
    static char noAp[] = "You don't have enough action points.";
    Object* stimpak = inven_pid_is_carried_ptr(obj_dude, PROTO_ID_STIMPACK);
    if (stimpak == nullptr) stimpak = inven_pid_is_carried_ptr(obj_dude, PROTO_ID_SUPER_STIMPACK);
    if (stimpak == nullptr) {
        display_print(noStimpak);
        return;
    }
    const int apCost = isInCombat() ? std::max(0, 4 - perk_level(PERK_QUICK_POCKETS)) : 0;
    if (apCost > obj_dude->data.critter.combat.ap) {
        display_print(noAp);
        return;
    }
    if (item_remove_mult(obj_dude, stimpak, 1) != 0) return;
    if (item_d_take_drug(obj_dude, stimpak) != 1) {
        item_add_force(obj_dude, stimpak, 1);
        return;
    }
    obj_connect(stimpak, obj_dude->tile, obj_dude->elevation, nullptr);
    obj_destroy(stimpak);
    if (apCost > 0) {
        obj_dude->data.critter.combat.ap -= apCost;
        intface_update_move_points(obj_dude->data.critter.combat.ap, combat_free_move);
    }
    intface_update_hit_points(true);
}

void first_person_action_menu()
{
    if (!first_person_world_input_allowed()) return;
    first_person_flush_render();
    // A native modal window above the frozen view; command execution happens
    // after closing it, through the same input loop as the original hotkeys.
    struct Command { const char* label; int key; };
    static const Command commands[] = {
        { "End turn [Space]", KEY_SPACE },
        { "End combat [Enter]", KEY_RETURN },
        { "Reload [R]", KEY_LOWERCASE_R },
        { "Switch hand [B]", KEY_LOWERCASE_B },
        { "Attack mode / aimed [N]", KEY_LOWERCASE_N },
        { "Use held item", -20 },
        { "Use stimpak", kFirstPersonUseStimpak },
        { "Inventory [I]", KEY_LOWERCASE_I },
        { "Skills [S]", KEY_LOWERCASE_S },
        { "Pip-Boy [P]", KEY_LOWERCASE_P },
        { "Character [C]", KEY_LOWERCASE_C },
        { "Automap [Tab]", KEY_TAB },
        { "Examine target [E]", KEY_LOWERCASE_E },
        { "Save [F4]", KEY_F4 },
        { "Load [F5]", KEY_F5 },
        { "Options [Esc]", KEY_ESCAPE },
        { "Move / interact / attack [M]", KEY_LOWERCASE_M },
    };
    constexpr int count = sizeof(commands) / sizeof(commands[0]);
    constexpr int rows = (count + 1) / 2;
    const int oldFont = text_curr();
    text_font(101);
    const int w = 520;
    const int rowHeight = std::max(24, text_height() + 10);
    const int h = 70 + rows * rowHeight;
    const int win = win_add((scr_size.lrx + 1 - w) / 2, (scr_size.lry + 1 - h) / 2,
        w, h, colorTable[0], WINDOW_MODAL | WINDOW_MOVE_ON_TOP);
    if (win == -1) { text_font(oldFont); return; }
    gFirstPersonActionMenuActive = true;
    const int previousCursor = gmouse_get_cursor();
    const bool restoreBackground = map_disable_bk_processes();
    gmouse_set_cursor(MOUSE_CURSOR_ARROW);
    for (int i = 0; i < count; i++) {
        win_register_button(win, 8 + (i / rows) * 256, 38 + (i % rows) * rowHeight,
            250, rowHeight, -1, -1, -1, 2000 + i, nullptr, nullptr, nullptr, 0);
    }
    int selected = 0;
    int command = -1;
    while (game_user_wants_to_quit == 0) {
        sharedFpsLimiter.mark();
        win_fill(win, 0, 0, w, h, colorTable[0]);
        win_print(win, "FIRST PERSON ACTIONS", w - 16, 8, 8, colorTable[992]);
        for (int i = 0; i < count; i++) {
            const int x = 8 + (i / rows) * 256;
            const int y = 38 + (i % rows) * rowHeight;
            if (i == selected) win_box(win, x, y, x + 248, y + rowHeight - 1, colorTable[992]);
            win_print(win, commands[i].label, 240, x + 4, y + 5, colorTable[992]);
        }
        win_print(win, "D-pad/Arrows: select  A/Enter: use  B/Esc: back", w - 16, 8, h - 22, colorTable[992]);
        win_draw(win);
        const int input = get_input();
        if (input == KEY_ESCAPE || input == KEY_F8) break;
        if (input == KEY_ARROW_UP) selected = (selected + count - 1) % count;
        if (input == KEY_ARROW_DOWN) selected = (selected + 1) % count;
        if (input == KEY_ARROW_LEFT || input == KEY_ARROW_RIGHT) {
            selected = std::min(count - 1, (selected + rows) % (2 * rows));
        }
        if (input == KEY_RETURN) { command = commands[selected].key; break; }
        if (input >= 2000 && input < 2000 + count) { command = commands[input - 2000].key; break; }
        renderPresent();
        sharedFpsLimiter.throttle();
    }
    win_delete(win);
    gFirstPersonActionMenuActive = false;
    if (restoreBackground) map_enable_bk_processes();
    gmouse_set_cursor(previousCursor);
    text_font(oldFont);
    gFirstPersonControllerTicks = 0;
    gFirstPersonMoveTicks = 0;
    first_person_render();
    first_person_heap_check("fp-action-menu");
    if (command == kFirstPersonUseStimpak && game_user_wants_to_quit == 0) {
        first_person_use_stimpak();
        first_person_render();
    } else if (command != -1 && game_user_wants_to_quit == 0) {
        GNW_add_input_buffer(command);
    }
}

FirstPersonModalScope::FirstPersonModalScope()
    : active_(first_person_is_enabled())
{
    if (active_) {
        first_person_heap_check("fp-modal-enter");
        first_person_suspend_overlay();
    }
}

FirstPersonModalScope::~FirstPersonModalScope()
{
    if (active_) {
        first_person_heap_check("fp-modal-exit");
        first_person_resume_overlay();
    }
}

bool first_person_overlay_visible()
{
    return gFirstPersonEnabled && !gFirstPersonOverlaySuspended
        && !gFirstPersonActionMenuActive && gFirstPersonWindow != -1
        && win_get_top_visible_win((scr_size.lrx + 1) / 2, (scr_size.lry + 1) / 2) == gFirstPersonWindow;
}

bool first_person_world_input_allowed()
{
    return first_person_overlay_visible()
        && obj_dude != nullptr && intface_is_enabled()
        && (!isInCombat() || (combat_state & COMBAT_STATE_0x02) != 0);
}

void first_person_notify(const char* message)
{
    if (!gFirstPersonEnabled || message == nullptr) return;
    std::snprintf(gFirstPersonNotice, sizeof(gFirstPersonNotice), "%s", message);
    gFirstPersonNoticeUntil = SDL_GetTicks64() + 6000;
    first_person_render();
}

void first_person_move(int rotation)
{
    if (!first_person_world_input_allowed()) return;
    const int ap = isInCombat() ? obj_dude->data.critter.combat.ap + combat_free_move : -1;
    if (ap == 0) return;
    const int destination = tile_num_in_direction(obj_dude->tile, rotation, 1);
    if (destination >= 0 && register_begin(ANIMATION_REQUEST_RESERVED) == 0) {
        register_object_move_to_tile(obj_dude, destination, obj_dude->elevation, ap, 0);
        register_end();
    }
    first_person_heap_check("fp-input-move");
}

void first_person_update()
{
    if (gFirstPersonNoticeUntil != 0 && SDL_GetTicks64() >= gFirstPersonNoticeUntil) {
        gFirstPersonNoticeUntil = 0;
        first_person_render();
    }
    gFirstPersonController = gamepad_controller();

    // Redraw when native code changes a mirrored interface-bar piece, such as
    // a new monitor message while the camera is still.
    if (first_person_overlay_visible() && interfaceWindow != -1) {
        const unsigned char* bar = win_get_buf(interfaceWindow);
        if (bar != nullptr) {
            static std::uint64_t lastHud = 0;
            const std::uint64_t hud = first_person_hud_signature(bar,
                win_width(interfaceWindow), win_height(interfaceWindow));
            // The Pip-Boy strip shows the native clock to the minute.
            static int lastClock = -1;
            const int clock = game_time_hour();
            if (hud != lastHud || clock != lastClock) {
                lastHud = hud;
                lastClock = clock;
                first_person_render();
            }
        }
    }

    if (!first_person_world_input_allowed()) {
        gFirstPersonControllerTicks = 0;
        return;
    }

    first_person_update_controller_move();

    if (first_person_update_controller_look()) {
        // First-person owns a full-screen presentation window now. Redraw that
        // window directly for camera motion instead of routing every stick
        // sample through Fallout's isometric map refresh/dirty-rect machinery.
        // The old path did extra work underneath the overlay and made free look
        // feel noticeably more stuttery than the 60 Hz gameplay loop.
        first_person_render();
    }
}

static void first_person_update_controller_move()
{
    if (!gFirstPersonEnabled || obj_dude == nullptr || gFirstPersonController == nullptr) {
        return;
    }

    auto normalizeAxis = [](Sint16 raw) {
        return raw >= 0 ? raw / 32767.0 : raw / 32768.0;
    };

    const double x = normalizeAxis(SDL_GameControllerGetAxis(
        gFirstPersonController,
        SDL_CONTROLLER_AXIS_LEFTX));
    const double y = normalizeAxis(SDL_GameControllerGetAxis(
        gFirstPersonController,
        SDL_CONTROLLER_AXIS_LEFTY));

    constexpr double kMoveDeadZone = 0.32;
    const double magnitude = std::sqrt(x * x + y * y);
    if (magnitude < kMoveDeadZone) {
        return;
    }

    const Uint64 now = SDL_GetTicks64();
    constexpr Uint64 kMoveRepeatMilliseconds = 120;
    if (gFirstPersonMoveTicks != 0
        && now - gFirstPersonMoveTicks < kMoveRepeatMilliseconds) {
        return;
    }

    // SDL Y is negative when pushing the stick forward. Convert the local
    // stick vector into a camera-relative angle, then quantize only the final
    // movement request to Fallout's six native hex directions.
    const double localForward = -y;
    const double localRight = x;
    constexpr double kPi = 3.14159265358979323846;
    const double localAngle = std::atan2(localRight, localForward);
    const double desiredHeading = gFirstPersonHeading + localAngle / (kPi / 12.0);
    int rotation = static_cast<int>(std::lround(desiredHeading / 4.0));
    rotation = ((rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;

    first_person_move(rotation);
    gFirstPersonMoveTicks = now;
}

static int first_person_horizon(int width, int height)
{
    return first_person_projected_horizon(width, height, gFirstPersonPitchDegrees);
}

static bool first_person_update_controller_look()
{
    if (!gFirstPersonEnabled) {
        return false;
    }

    gFirstPersonController = gamepad_controller();

    const Uint64 now = SDL_GetTicks64();
    if (gFirstPersonControllerTicks == 0) {
        gFirstPersonControllerTicks = now;
        return false;
    }

    const double dt = std::min(
        0.05,
        static_cast<double>(now - gFirstPersonControllerTicks) / 1000.0);
    gFirstPersonControllerTicks = now;

    if (gFirstPersonController == nullptr || dt <= 0.0) {
        return false;
    }

    auto normalizeAxis = [](Sint16 raw) {
        return raw >= 0 ? raw / 32767.0 : raw / 32768.0;
    };

    double yawAxis = normalizeAxis(SDL_GameControllerGetAxis(
        gFirstPersonController,
        SDL_CONTROLLER_AXIS_RIGHTX));
    double pitchAxis = normalizeAxis(SDL_GameControllerGetAxis(
        gFirstPersonController,
        SDL_CONTROLLER_AXIS_RIGHTY));

    auto applyDeadZone = [](double axis) {
        constexpr double deadZone = 0.18;
        if (std::abs(axis) <= deadZone) {
            return 0.0;
        }
        return std::copysign(
            (std::abs(axis) - deadZone) / (1.0 - deadZone),
            axis);
    };

    yawAxis = applyDeadZone(yawAxis);
    pitchAxis = applyDeadZone(pitchAxis);
    if (yawAxis == 0.0 && pitchAxis == 0.0) {
        return false;
    }

    // Horizontal look should feel like a camera, not a repeated 15-degree
    // turn command. Keep fine control near center, but allow a much faster
    // room-scale sweep at full deflection.
    constexpr double kHeadingUnitsPerSecond = 18.0;
    constexpr double kHeadingCount = ROTATION_COUNT * 4.0;
    gFirstPersonHeading = std::fmod(
        gFirstPersonHeading + yawAxis * kHeadingUnitsPerSecond * dt,
        kHeadingCount);
    if (gFirstPersonHeading < 0.0) {
        gFirstPersonHeading += kHeadingCount;
    }

    // SDL's right-stick Y axis is negative when pushed up. Treat that as
    // positive camera pitch. The downward horizon may leave the viewport so
    // nearby floor-level critters can reach the center reticle.
    constexpr double kPitchDegreesPerSecond = 180.0;
    constexpr double kPitchUpLimitDegrees = 18.0;
    constexpr double kPitchDownLimitDegrees = 55.0;
    gFirstPersonPitchDegrees = std::clamp(
        gFirstPersonPitchDegrees - pitchAxis * kPitchDegreesPerSecond * dt,
        -kPitchDownLimitDegrees,
        kPitchUpLimitDegrees);

    gFirstPersonCameraRevision++;
    gFirstPersonPicks.clear();
    gFirstPersonInteractionPicks.clear();
    return true;
}

// Input coordinates are desktop/window coordinates, whereas projection uses
// viewport-local coordinates. Keep a single conversion for highlight and input.
int first_person_target_tile(int screenX, int screenY)
{
    const int viewWindow = first_person_window();
    if (!gFirstPersonEnabled || obj_dude == nullptr || viewWindow == -1) {
        return -1;
    }
    Rect rect;
    if (win_get_rect(viewWindow, &rect) != 0) {
        return -1;
    }
    const int width = win_width(viewWindow);
    const int height = win_height(viewWindow);
    const int x = screenX - rect.ulx;
    const int y = screenY - rect.uly;
    if (width <= 0 || height <= 0) {
        return -1;
    }
    const int horizon = first_person_horizon(width, height);
    if (x < 0 || x >= width || y <= horizon || y >= height) {
        return -1;
    }
    const double focal = width * 0.70;
    const double z = focal * kFirstPersonEyeHeight / (y - horizon);
    if (z < 0.45 || z > 36.0) {
        return -1;
    }
    const double cameraX = (x - width * 0.5) * z / focal;
    constexpr double pi = 3.14159265358979323846;
    const double yaw = -pi / 6.0 + gFirstPersonHeading * pi / 12.0;
    const double dx = -std::sin(yaw) * cameraX + std::cos(yaw) * z;
    const double dy = std::cos(yaw) * cameraX + std::sin(yaw) * z;
    int isoX;
    int isoY;
    if (tile_coord(obj_dude->tile, &isoX, &isoY, map_elevation) != 0) {
        return -1;
    }
    isoX += static_cast<int>(std::lround(27.712812921102035 * dx + 16.0 * dy));
    isoY += static_cast<int>(std::lround(-6.928203230275509 * dx + 12.0 * dy));
    return tile_num(isoX, isoY, map_elevation, false);
}

Object* first_person_object_at(int screenX, int screenY, int objectType, bool includeDude, int elevation)
{
    const int viewWindow = first_person_window();
    if (!gFirstPersonEnabled || obj_dude == nullptr || viewWindow == -1
        || elevation != map_elevation || elevation != gPickElevation
        || obj_dude->tile != gPickTile || gFirstPersonCameraRevision != gPickRotation
        || gPickWidth != win_width(viewWindow) || gPickHeight != win_height(viewWindow)
        || gFirstPersonPicks.empty() || gFirstPersonInteractionPicks.empty()) {
        return nullptr;
    }
    Rect rect;
    if (win_get_rect(viewWindow, &rect) != 0) {
        return nullptr;
    }
    const int x = screenX - rect.ulx;
    const int y = screenY - rect.uly;
    if (x < 0 || x >= gPickWidth || y < 0 || y >= gPickHeight) {
        return nullptr;
    }

    auto resolvePick = [&](const FirstPersonPick& pick, bool allowWall) -> Object* {
        if (pick.object == nullptr) {
            return nullptr;
        }

        for (Object* object = obj_find_first_at(elevation);
             object != nullptr;
             object = obj_find_next_at()) {
            if (object != pick.object || object->id != pick.id
                || (object->flags & OBJECT_HIDDEN) != 0
                || (!includeDude && object == obj_dude)
                || (objectType != -1 && FID_TYPE(object->fid) != objectType)) {
                continue;
            }

            // Generic first-person interaction snapping should not pull the
            // pointer sideways onto a wall merely because a small object has
            // transparent pixels around it. Explicit wall queries still work.
            if (!allowWall && FID_TYPE(object->fid) == OBJ_TYPE_WALL) {
                return nullptr;
            }

            return object;
        }

        return nullptr;
    };

    // Exact visible-pixel targeting is always authoritative.
    const FirstPersonPick exactPick = gFirstPersonPicks[y * gPickWidth + x];
    if (Object* exact = resolvePick(exactPick, true)) {
        return exact;
    }

    // Billboard objects also have a depth-tested interaction footprint based
    // on the opaque bounds of their source FRM. At close range, transparent
    // holes in isometric art can scale far beyond a fixed pixel halo; this
    // proxy keeps the object selectable without changing what is actually
    // rendered or bypassing Fallout's native object/use/combat logic.
    const FirstPersonPick proxyPick =
        gFirstPersonInteractionPicks[y * gPickWidth + x];
    if (Object* proxy = resolvePick(proxyPick, false)) {
        return proxy;
    }

    // Fallout's original sprites are sparse isometric silhouettes. In first
    // person, a literal one-pixel hit test makes small switches, items and
    // distant critters unnecessarily hard to select. Search a tiny screen-space
    // halo only when the exact pixel was empty. This remains scene-agnostic:
    // the returned live Object still flows through Fallout's normal use/combat
    // code, scripts, AP checks, locks and animation.
    constexpr int kPickAssistRadius = 6;
    Object* best = nullptr;
    int bestDistanceSquared = kPickAssistRadius * kPickAssistRadius + 1;

    for (int dy = -kPickAssistRadius; dy <= kPickAssistRadius; dy++) {
        const int py = y + dy;
        if (py < 0 || py >= gPickHeight) {
            continue;
        }

        for (int dx = -kPickAssistRadius; dx <= kPickAssistRadius; dx++) {
            const int distanceSquared = dx * dx + dy * dy;
            if (distanceSquared == 0
                || distanceSquared > kPickAssistRadius * kPickAssistRadius
                || distanceSquared >= bestDistanceSquared) {
                continue;
            }

            const int px = x + dx;
            if (px < 0 || px >= gPickWidth) {
                continue;
            }

            FirstPersonPick nearbyPick =
                gFirstPersonPicks[py * gPickWidth + px];
            if (nearbyPick.object == nullptr) {
                nearbyPick = gFirstPersonInteractionPicks[py * gPickWidth + px];
            }
            Object* candidate = resolvePick(
                nearbyPick,
                objectType == OBJ_TYPE_WALL);
            if (candidate != nullptr) {
                best = candidate;
                bestDistanceSquared = distanceSquared;
            }
        }
    }

    return best;
}

// The topology table cannot identify the shape of generic scenery. Export
// unique source frames on this elevation, only during an explicit map dump.
// Indexed pixels preserve transparency (index 0); palette components are 0..63.
static void first_person_dump_art(const char* mapPath)
{
    char path[4096];
    const int length = std::snprintf(path, sizeof(path), "%s.art.txt", mapPath);
    if (length < 0 || length >= static_cast<int>(sizeof(path))) {
        return;
    }
    FILE* output = std::fopen(path, "w");
    if (output == nullptr) {
        std::perror("First-person art dump");
        return;
    }
    std::fprintf(output, "# palette_rgb6=");
    for (unsigned char component : cmap) {
        std::fprintf(output, "%02x", static_cast<unsigned int>(component));
    }
    std::fprintf(output, "\n# transparent_index=0\n");
    std::fprintf(output, "type\tfid\tdirection\tframe\tart\twidth\theight\tpixels_hex\n");
    struct FrameKey { int fid; int direction; int frame; };
    std::vector<FrameKey> exported;
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr; object = obj_find_next_at()) {
        const int type = FID_TYPE(object->fid);
        if ((type != OBJ_TYPE_WALL && type != OBJ_TYPE_SCENERY)
            || object->tile < 0) {
            continue;
        }
        const int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        if (std::any_of(exported.begin(), exported.end(), [&](const FrameKey& key) {
                return key.fid == object->fid && key.direction == direction && key.frame == object->frame;
            })) {
            continue;
        }
        exported.push_back({ object->fid, direction, object->frame });
        char name[64] = { 0 };
        art_get_base_name(type, object->fid & 0xFFF, name);
        CacheEntry* entry = nullptr;
        Art* art = art_ptr_lock(object->fid, &entry);
        if (art == nullptr) {
            continue;
        }
        const int number = std::clamp(object->frame, 0, std::max(0, art_frame_max_frame(art) - 1));
        ArtFrame* frame = frame_ptr(art, number, direction);
        unsigned char* pixels = art_frame_data(art, number, direction);
        if (frame != nullptr && pixels != nullptr && frame->width > 0 && frame->height > 0) {
            std::fprintf(output, "%d\t%d\t%d\t%d\t%s\t%d\t%d\t",
                type, object->fid, direction, number, name, frame->width, frame->height);
            for (int i = 0; i < frame->width * frame->height; i++) {
                std::fprintf(output, "%02x", static_cast<unsigned int>(pixels[i]));
            }
            std::fprintf(output, "\n");
        }
        art_ptr_unlock(entry);
    }
    const bool failed = std::ferror(output) != 0;
    const int closeResult = std::fclose(output);
    std::fprintf(stderr, "First-person art dump %s: %s\n",
        failed || closeResult != 0 ? "failed" : "saved", path);
}

// Opt-in snapshot for inspecting actual map topology rather than inferring it
// from a filmed viewport. Export once per process, on the first FP frame after
// loading a save. Include hidden/invisible walls and scenery (including doors).
static void first_person_dump_map()
{
    static bool attempted = false;
    const char* path = std::getenv("FALLOUT_FP_MAP_DUMP");
    if (attempted || path == nullptr || *path == '\0') {
        return;
    }
    attempted = true;
    FILE* output = std::fopen(path, "w");
    if (output == nullptr) {
        std::perror("First-person map dump");
        return;
    }
    std::fprintf(output, "# map=%.16s elevation=%d player_tile=%d rotation=%d\n",
        map_data.name, map_elevation, obj_dude->tile, obj_dude->rotation);
    std::fprintf(output, "id\ttype\ttile\trotation\tfid\tpid\tflags\tproto_flags\textended_flags\tscenery_type\tart\twidth\theight\tframe_x\tframe_y\tneighbor0\tneighbor1\tneighbor2\tneighbor3\tneighbor4\tneighbor5\n");
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr; object = obj_find_next_at()) {
        const int type = FID_TYPE(object->fid);
        if (type != OBJ_TYPE_WALL && type != OBJ_TYPE_SCENERY) {
            continue;
        }
        Proto* proto = nullptr;
        unsigned int protoFlags = 0;
        unsigned int extendedFlags = 0;
        int sceneryType = -1;
        if (proto_ptr(object->pid, &proto) == 0 && proto != nullptr) {
            if (type == OBJ_TYPE_WALL && PID_TYPE(object->pid) == OBJ_TYPE_WALL) {
                protoFlags = proto->wall.flags;
                extendedFlags = proto->wall.extendedFlags;
            } else if (type == OBJ_TYPE_SCENERY && PID_TYPE(object->pid) == OBJ_TYPE_SCENERY) {
                protoFlags = proto->scenery.flags;
                extendedFlags = proto->scenery.extendedFlags;
                sceneryType = proto->scenery.type;
            }
        }
        char artName[64] = { 0 };
        art_get_base_name(type, object->fid & 0xFFF, artName);
        int width = 0, height = 0, frameX = 0, frameY = 0;
        CacheEntry* entry = nullptr;
        Art* art = art_ptr_lock(object->fid, &entry);
        if (art != nullptr) {
            const int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
            const int number = std::clamp(object->frame, 0, std::max(0, art_frame_max_frame(art) - 1));
            ArtFrame* frame = frame_ptr(art, number, direction);
            if (frame != nullptr) {
                width = frame->width;
                height = frame->height;
                frameX = frame->x;
                frameY = frame->y;
            }
            art_ptr_unlock(entry);
        }
        std::fprintf(output, "%d\t%d\t%d\t%d\t%d\t%d\t%08x\t%08x\t%08x\t%d\t%s\t%d\t%d\t%d\t%d",
            object->id, type, object->tile, object->rotation, object->fid,
            object->pid, static_cast<unsigned int>(object->flags), protoFlags,
            extendedFlags, sceneryType, artName, width, height, frameX, frameY);
        for (int direction = 0; direction < ROTATION_COUNT; direction++) {
            const int neighbor = object->tile >= 0 && object->tile < 40000
                ? tile_num_in_direction(object->tile, direction, 1) : -1;
            std::fprintf(output, "\t%d", neighbor);
        }
        std::fprintf(output, "\n");
    }
    const bool writeFailed = std::ferror(output) != 0;
    const int closeResult = std::fclose(output);
    if (writeFailed || closeResult != 0) {
        std::fprintf(stderr, "First-person map dump failed: %s\n", path);
    } else {
        std::fprintf(stderr, "First-person map dump saved: %s\n", path);
        first_person_dump_art(path);
    }
}

static void first_person_render_now();

void first_person_render()
{
    gFirstPersonScene.request();
}

void first_person_flush_render()
{
    if (!gFirstPersonScene.take(gFirstPersonEnabled, gFirstPersonOverlaySuspended)) return;
    first_person_heap_check_frame();
    first_person_heap_check("fp-frame-begin");
    first_person_render_now();
    first_person_heap_check(gFirstPersonLastFrameGpu ? "fp-frame-gpu" : "fp-frame-software");
}

// Squares that get a synthesized ceiling on an elevation without roof tiles
// (see first_person_ceiling.h). Walls, doors and invisible blockers bound
// the indoors. Cached per map elevation; empty when the elevation has roofs.
static const std::vector<unsigned char>& first_person_enclosed_ceiling(int elevation)
{
    static char cachedMap[sizeof(map_data.name)] = {};
    static int cachedElevation = -1;
    static std::vector<unsigned char> squares;
    if (cachedElevation == elevation && std::strncmp(cachedMap, map_data.name, sizeof(cachedMap)) == 0) {
        return squares;
    }
    std::memcpy(cachedMap, map_data.name, sizeof(cachedMap));
    cachedElevation = elevation;
    squares.assign(kFirstPersonSquareCount, 0);

    const int noRoofFid = art_id(OBJ_TYPE_TILE, 1, 0, 0, 0);
    for (int i = 0; i < SQUARE_GRID_SIZE; i++) {
        if (art_id(OBJ_TYPE_TILE, (square[elevation]->field_0[i] >> 16) & 0xFFF, 0, 0, 0) != noRoofFid) {
            return squares;
        }
    }
    std::vector<unsigned char> barrier(kFirstPersonTileCount, 0);
    for (Object* object = obj_find_first_at(elevation); object != nullptr; object = obj_find_next_at()) {
        if (object->tile < 0 || object->tile >= kFirstPersonTileCount) continue;
        const int type = FID_TYPE(object->fid);
        if (type == OBJ_TYPE_WALL) {
            barrier[object->tile] = kFirstPersonWallBarrier;
        } else if (type == OBJ_TYPE_SCENERY && barrier[object->tile] == 0) {
            // Doors and blocking scenery (cave rock, invisible blockers) bound
            // the indoors too, as they bound native movement.
            Proto* proto = nullptr;
            const bool door = PID_TYPE(object->pid) == OBJ_TYPE_SCENERY && proto_ptr(object->pid, &proto) == 0
                && proto != nullptr && proto->scenery.type == SCENERY_TYPE_DOOR;
            if (door || (object->flags & OBJECT_NO_BLOCK) == 0) barrier[object->tile] = kFirstPersonBarrier;
        }
    }
    squares = first_person_enclosed_squares(barrier, first_person_hex_neighbor);
    return squares;
}

static void first_person_render_now()
{
    gFirstPersonLastFrameGpu = false;
    const int viewWindow = first_person_window();
    if (!gFirstPersonEnabled || obj_dude == nullptr || viewWindow == -1) {
        return;
    }

    unsigned char* buffer = win_get_buf(viewWindow);
    if (buffer == nullptr) {
        return;
    }

    const int width = win_width(viewWindow);
    const int height = win_height(viewWindow);
    if (width <= 0 || height <= 0) {
        return;
    }

    first_person_gpu_begin_frame();
    first_person_dump_map();

    gPickWidth = width;
    gPickHeight = height;
    gPickTile = obj_dude->tile;
    gPickRotation = gFirstPersonCameraRevision;
    gPickElevation = map_elevation;
    const size_t pixelCount = static_cast<size_t>(width) * height;
    if (gFirstPersonPicks.size() != pixelCount) {
        gFirstPersonPicks.resize(pixelCount);
        gFirstPersonInteractionPicks.resize(pixelCount);
        gFirstPersonDepthBuffer.resize(pixelCount);
        gFirstPersonInteractionDepth.resize(pixelCount);
    }
    std::fill(gFirstPersonPicks.begin(), gFirstPersonPicks.end(), FirstPersonPick { nullptr, -1 });
    std::fill(
        gFirstPersonInteractionPicks.begin(),
        gFirstPersonInteractionPicks.end(),
        FirstPersonPick { nullptr, -1 });

    const int sky = colorTable[0];
    const int ground = colorTable[10570];
    const int gridColor = colorTable[992];

    const int horizon = first_person_horizon(width, height);
    const int backgroundSplit = first_person_background_split(height, horizon);
    buf_fill(buffer, width, backgroundSplit, width, sky);
    buf_fill(buffer + backgroundSplit * width, width, height - backgroundSplit, width, ground);

    static const bool debugWalls = std::getenv("FALLOUT_FP_WALL_DEBUG") != nullptr;
    const bool gpuWorld = !debugWalls && first_person_world_gpu_begin(
        width, height, horizon, static_cast<unsigned char>(sky), static_cast<unsigned char>(ground));
    gFirstPersonLastFrameGpu = gpuWorld;
    std::vector<FirstPersonPick> gpuOwners { { nullptr, -1 } };
    auto gpuOwner = [&](Object* object) -> std::uint32_t {
        gpuOwners.push_back({ object, object->id });
        return static_cast<std::uint32_t>(gpuOwners.size() - 1);
    };
    auto gpuVertex = [](double x, double y, double z, double u, double v) {
        return FirstPersonGpuVertex { static_cast<float>(x * z),
            static_cast<float>(y * z), static_cast<float>(z),
            static_cast<float>(u), static_cast<float>(v) };
    };

    constexpr int kHexGridWidth = 200;
    // Visibility and structural evidence are separate. Keep enough topology
    // outside the draw radius for the six-cell bridge/material searches.
    constexpr int kRenderRadius = 48;
    constexpr int kTopologyRadius = kRenderRadius + 6;
    constexpr double kSqrt3Over2 = 0.8660254037844386;
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kNearPlane = 0.45;
    constexpr double kFarPlane = 36.0;

    auto tileToWorld = [](int tile, double* x, double* y) {
        const int column = tile % kHexGridWidth;
        const int row = tile / kHexGridWidth;
        *x = -column * kSqrt3Over2;
        *y = row - (column & 1) * 0.5;
    };

    double playerWorldX;
    double playerWorldY;
    tileToWorld(obj_dude->tile, &playerWorldX, &playerWorldY);

    const double yaw = -kPi / 6.0 + gFirstPersonHeading * (kPi / 12.0);
    const double forwardX = std::cos(yaw);
    const double forwardY = std::sin(yaw);
    const double rightX = -forwardY;
    const double rightY = forwardX;
    const double focal = width * 0.70;

    // v0.012: a small software depth buffer shared by the first-person passes.
    // Fallout's original renderer can rely on isometric draw order; once we
    // project sprites into perspective that is no longer enough. Keeping depth
    // per framebuffer pixel gives later wall geometry a proper foundation and
    // stops distant cardboard sprites from drawing through nearer ones.
    std::fill(
        gFirstPersonDepthBuffer.begin(),
        gFirstPersonDepthBuffer.end(),
        kFarPlane + 1.0);
    std::fill(
        gFirstPersonInteractionDepth.begin(),
        gFirstPersonInteractionDepth.end(),
        kFarPlane + 1.0);
    std::vector<double>& depthBuffer = gFirstPersonDepthBuffer;
    std::vector<double>& interactionDepth = gFirstPersonInteractionDepth;

    // v0.009: perspective-map Fallout's real floor tiles onto the ground.
    //
    // Rather than inventing a second floor coordinate system, convert each
    // first-person ground sample back into the isometric screen coordinates
    // that Fallout already uses. square_num/square_coord then tell us exactly
    // which original floor FRM and which pixel belongs at that location.
    int playerIsoX = 0;
    int playerIsoY = 0;
    tile_coord(obj_dude->tile, &playerIsoX, &playerIsoY, map_elevation);

    std::vector<FirstPersonFloorArt> floorArts;
    auto getFloorArt = [&floorArts](int fid) -> FirstPersonFloorArt* {
        for (FirstPersonFloorArt& entry : floorArts) {
            if (entry.fid == fid) {
                return &entry;
            }
        }

        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(fid, &cacheEntry);
        if (art == nullptr) {
            return nullptr;
        }

        ArtFrame* frame = frame_ptr(art, 0, 0);
        unsigned char* pixels = art_frame_data(art, 0, 0);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        floorArts.push_back({ fid, art, frame, pixels, cacheEntry });
        return &floorArts.back();
    };

    constexpr double kEyeHeight = kFirstPersonEyeHeight;
    // Roofs become ceilings at the structural wall-top height. Each square's
    // roof shares its floor's footprint; the isometric view only draws it 96
    // pixels higher. Tile 1 is the native "no roof" tile.
    constexpr double kCeilingHeight = 1.65;
    const int noRoofFid = art_id(OBJ_TYPE_TILE, 1, 0, 0, 0);
    auto roofFid = [noRoofFid](int squareData) {
        const int fid = art_id(OBJ_TYPE_TILE, (squareData >> 16) & 0xFFF, 0, 0, 0);
        return fid == noRoofFid ? -1 : fid;
    };
    // Roofless elevations: enclosed squares use their own floor tile,
    // darkened through the native intensity table, as the ceiling.
    const std::vector<unsigned char>& enclosedCeiling = first_person_enclosed_ceiling(map_elevation);
    constexpr int kSynthesizedCeilingIntensity = 80; // Of 128.
    auto darken = [](unsigned char pixel) {
        const unsigned char dark = intensityColorTable[pixel][kSynthesizedCeilingIntensity];
        return dark != 0 ? dark : pixel;
    };
    constexpr double kIsoXFromWorldX = 27.712812921102035;
    constexpr double kIsoYFromWorldX = -6.928203230275509;

    // Transitional performance path while the floor is being migrated to GPU
    // geometry. Sample one perspective point per 2x2 output block instead of
    // repeating the expensive world/square/art lookup for every single pixel.
    // The game is already palette/pixel-art based, so this is a useful speed
    // bridge without changing the authoritative projection or world mapping.
    if (gpuWorld) {
        struct FloorFace { int fid; bool dark; int width; int height; std::vector<unsigned char> pixels; };
        std::vector<FloorFace> floorFaces;
        for (int tile = 0; tile < SQUARE_GRID_SIZE; tile++) {
            int ix = 0, iy = 0;
            if (square_coord(tile, &ix, &iy, map_elevation) != 0) continue;
            // Cull before touching art. A floor FRM occupies about 3 world units.
            const double isoDx = ix - playerIsoX, isoDy = iy - playerIsoY;
            const double dx = (12.0 * isoDx - 16.0 * isoDy) / (kIsoXFromWorldX * 12.0 - 16.0 * kIsoYFromWorldX);
            const double dy = (-kIsoYFromWorldX * isoDx + kIsoXFromWorldX * isoDy) / (kIsoXFromWorldX * 12.0 - 16.0 * kIsoYFromWorldX);
            const double z = dx * forwardX + dy * forwardY;
            if (z < -4.0 || z > kFarPlane + 4.0 || std::hypot(dx, dy) > kFarPlane + 4.0) continue;
            const int squareData = square[map_elevation]->field_0[tile];
            const int floorFid = art_id(OBJ_TYPE_TILE, squareData & 0xFFF, 0, 0, 0);
            int ceilingFid = roofFid(squareData);
            bool darkCeiling = false;
            if (ceilingFid == -1 && enclosedCeiling[tile]) {
                ceilingFid = floorFid;
                darkCeiling = true;
            }
            for (int layer = 0; layer < 2; layer++) {
                const int fid = layer == 0 ? floorFid : ceilingFid;
                const bool dark = layer == 1 && darkCeiling;
                if (fid == -1) continue;
                const double planeHeight = layer == 0 ? kEyeHeight : kEyeHeight - kCeilingHeight;
                FirstPersonFloorArt* art = getFloorArt(fid);
                if (!art) continue;
                const double sx[4] = { 0.0, static_cast<double>(art->frame->width), static_cast<double>(art->frame->width), 0.0 };
                const double sy[4] = { 0.0, 0.0, static_cast<double>(art->frame->height), static_cast<double>(art->frame->height) };
                FirstPersonGpuVertex vertices[4];
                for (int i = 0; i < 4; i++) {
                    const double a = isoDx + sx[i], b = isoDy + sy[i];
                    const double wx = (12.0 * a - 16.0 * b) / (kIsoXFromWorldX * 12.0 - 16.0 * kIsoYFromWorldX);
                    const double wy = (-kIsoYFromWorldX * a + kIsoXFromWorldX * b) / (kIsoXFromWorldX * 12.0 - 16.0 * kIsoYFromWorldX);
                    const double cx = wx * rightX + wy * rightY, cz = wx * forwardX + wy * forwardY;
                    vertices[i] = { static_cast<float>(width * 0.5 * cz + focal * cx),
                        static_cast<float>(horizon * cz + focal * planeHeight), static_cast<float>(cz),
                        (i == 1 || i == 2) ? 1.0f : 0.0f, (i >= 2) ? 1.0f : 0.0f };
                }
                // Match native square ownership rather than drawing overlapping
                // rectangular floor FRMs as competing pieces of geometry.
                FloorFace* face = nullptr;
                for (auto& cached : floorFaces) {
                    if (cached.fid == fid && cached.dark == dark) { face = &cached; break; }
                }
                if (!face) {
                    FloorFace masked { fid, dark, art->frame->width, art->frame->height, {} };
                    masked.pixels.assign(art->pixels, art->pixels + masked.width * masked.height);
                    for (int y = 0; y < masked.height; y++) {
                        for (int x = 0; x < masked.width; x++) {
                            unsigned char& pixel = masked.pixels[y * masked.width + x];
                            if (square_num(ix + x, iy + y, map_elevation) != tile) {
                                pixel = 0;
                            } else if (dark && pixel != 0) {
                                pixel = darken(pixel);
                            }
                        }
                    }
                    floorFaces.push_back(std::move(masked)); face = &floorFaces.back();
                }
                first_person_world_gpu_quad(face->pixels.data(), face->width, face->height, vertices, 0, false, static_cast<float>(kFarPlane));
            }
        }
    }
    constexpr int kFloorSampleStep = 2;
    // Rows below the horizon sample the floor; rows above it the ceiling.
    for (int screenY = 0; !gpuWorld && screenY < height; screenY += kFloorSampleStep) {
        if (screenY == horizon) continue;
        const bool ceiling = screenY < horizon;
        const double cameraZ = ceiling ? focal * (kCeilingHeight - kEyeHeight) / (horizon - screenY)
                                       : focal * kEyeHeight / (screenY - horizon);
        if (cameraZ < kNearPlane || cameraZ > kFarPlane) {
            continue;
        }

        for (int screenX = 0; screenX < width; screenX += kFloorSampleStep) {
            const double cameraX = (screenX - width * 0.5) * cameraZ / focal;

            const double worldDx = rightX * cameraX + forwardX * cameraZ;
            const double worldDy = rightY * cameraX + forwardY * cameraZ;

            // These are the inverse of the hex-world basis used above:
            // +1 world Y = (+16,+12) isometric pixels.
            // +1 world X = (+27.713,-6.928) isometric pixels.
            const int isoX = playerIsoX + static_cast<int>(std::lround(kIsoXFromWorldX * worldDx + 16.0 * worldDy));
            const int isoY = playerIsoY + static_cast<int>(std::lround(kIsoYFromWorldX * worldDx + 12.0 * worldDy));

            const int squareTile = square_num(isoX, isoY, map_elevation);
            if (squareTile < 0 || squareTile >= SQUARE_GRID_SIZE) {
                continue;
            }

            const int floorData = square[map_elevation]->field_0[squareTile];
            int fid = ceiling ? roofFid(floorData) : art_id(OBJ_TYPE_TILE, floorData & 0xFFF, 0, 0, 0);
            const bool dark = ceiling && fid == -1 && enclosedCeiling[squareTile];
            if (dark) {
                fid = art_id(OBJ_TYPE_TILE, floorData & 0xFFF, 0, 0, 0);
            }
            if (fid == -1) {
                continue;
            }

            int floorIsoX = 0;
            int floorIsoY = 0;
            if (square_coord(squareTile, &floorIsoX, &floorIsoY, map_elevation) != 0) {
                continue;
            }

            FirstPersonFloorArt* floorArt = getFloorArt(fid);
            if (floorArt == nullptr) {
                continue;
            }

            const int sourceX = isoX - floorIsoX;
            const int sourceY = isoY - floorIsoY;
            if (sourceX < 0 || sourceX >= floorArt->frame->width
                || sourceY < 0 || sourceY >= floorArt->frame->height) {
                continue;
            }

            unsigned char pixel = floorArt->pixels[sourceY * floorArt->frame->width + sourceX];
            if (pixel != 0 && dark) {
                pixel = darken(pixel);
            }
            if (pixel != 0) {
                for (int blockY = 0; blockY < kFloorSampleStep && screenY + blockY < height; blockY++) {
                    for (int blockX = 0; blockX < kFloorSampleStep && screenX + blockX < width; blockX++) {
                        const int destination =
                            (screenY + blockY) * width + screenX + blockX;
                        buffer[destination] = pixel;
                        depthBuffer[destination] = cameraZ;
                    }
                }
            }
        }
    }

    for (FirstPersonFloorArt& entry : floorArts) {
        art_ptr_unlock(entry.cacheEntry);
    }

    // v0.014: first-person mouse-to-world targeting.
    //
    // The mouse position defines a camera ray. Intersect that ray with the
    // ground plane, convert the hit back through Fallout's isometric mapping,
    // then let tile_num resolve the actual engine hex. The highlighted hex is
    // therefore Fallout's own tile, not a second approximation of the map.
    int mouseX = width / 2;
    int mouseY = height / 2;
    mouse_get_position(&mouseX, &mouseY);

    const int targetTile = first_person_target_tile(mouseX, mouseY);
    Rect viewportRect;
    if (win_get_rect(display_win, &viewportRect) == 0) {
        mouseX -= viewportRect.ulx;
        mouseY -= viewportRect.uly;
    }

    int gpuHexX[6] {}, gpuHexY[6] {};
    bool gpuHexVisible = false;
    if (targetTile >= 0) {
        double targetWorldX;
        double targetWorldY;
        tileToWorld(targetTile, &targetWorldX, &targetWorldY);

        constexpr double kHexRadius = 0.32;
        int hexX[6];
        int hexY[6];
        bool hexVisible = true;
        for (int corner = 0; corner < 6; corner++) {
            const double angle = corner * kPi / 3.0;
            const double worldX = targetWorldX + std::cos(angle) * kHexRadius;
            const double worldY = targetWorldY + std::sin(angle) * kHexRadius;
            const double dx = worldX - playerWorldX;
            const double dy = worldY - playerWorldY;
            const double cameraX = dx * rightX + dy * rightY;
            const double cameraZ = dx * forwardX + dy * forwardY;
            if (cameraZ <= kNearPlane) {
                hexVisible = false;
                break;
            }
            hexX[corner] = width / 2 + static_cast<int>(cameraX * focal / cameraZ);
            hexY[corner] = horizon + static_cast<int>(focal * kEyeHeight / cameraZ);
            // Fallout's software draw_line routine does not clip endpoints.
            // Never hand it an off-screen coordinate: doing so can write past
            // the framebuffer and crash the game. For now, hide a partially
            // off-screen target hex rather than trying to draw only part of it.
            if (hexX[corner] < 0 || hexX[corner] >= width
                || hexY[corner] < 0 || hexY[corner] >= height) {
                hexVisible = false;
                break;
            }
        }

        if (gpuWorld && hexVisible) {
            std::copy(hexX, hexX + 6, gpuHexX);
            std::copy(hexY, hexY + 6, gpuHexY);
            gpuHexVisible = true;
        }
        if (hexVisible && !gpuWorld) {
            const int highlightColor = colorTable[31744];
            for (int corner = 0; corner < 6; corner++) {
                const int next = (corner + 1) % 6;
                draw_line(buffer, width, hexX[corner], hexY[corner], hexX[next], hexY[next], highlightColor);
            }
        }
    }

    // Retain sparse depth guides for this build. They make it easy to see
    // whether the newly projected floor agrees with our established geometry.
    first_person_draw_depth_guides(buffer, width, height, horizon, gridColor);

    // Debug geometry uses the same clipping/depth path as textured walls.
    auto debugWallColor = [](FirstPersonWallKind kind) {
        switch (kind) {
        case FIRST_PERSON_WALL_NORTH_SOUTH:
            return 31744; // red
        case FIRST_PERSON_WALL_EAST_WEST:
            return 992; // green
        case FIRST_PERSON_WALL_NORTH_CORNER:
            return 31; // blue
        case FIRST_PERSON_WALL_SOUTH_CORNER:
            return 32736; // yellow
        case FIRST_PERSON_WALL_EAST_CORNER:
            return 31775; // magenta
        case FIRST_PERSON_WALL_WEST_CORNER:
            return 1023; // cyan
        case FIRST_PERSON_WALL_UNKNOWN:
            return 32767; // white
        }
        return 31744;
    };

    std::vector<FirstPersonWallSprite> walls;
    std::vector<FirstPersonWallSprite> blockWallHints;
    for (Object* wall = obj_find_first_at(map_elevation);
         wall != nullptr;
         wall = obj_find_next_at()) {
        if (wall == obj_dude
            || wall->tile < 0
            || wall->tile >= kHexGridWidth * kHexGridWidth
            || (wall->flags & OBJECT_HIDDEN) != 0
            || FID_TYPE(wall->fid) != OBJ_TYPE_WALL
            || tile_dist(obj_dude->tile, wall->tile) > kTopologyRadius) {
            continue;
        }

        // block.frm is invisible collision/topology data. Do not render its
        // 1x1 art, but retain its tile: a chain of blocker cells between two
        // compatible visible walls is strong evidence for a wall span that the
        // original isometric artwork supplied only through overlap.
        const int frmId = wall->fid & 0xFFF;
        char artName[64] = { 0 };
        if (art_get_base_name(OBJ_TYPE_WALL, frmId, artName) == -1) {
            continue;
        }
        if (std::strcmp(artName, "block.frm") == 0) {
            double blockWorldX;
            double blockWorldY;
            tileToWorld(wall->tile, &blockWorldX, &blockWorldY);
            blockWallHints.push_back({
                wall,
                wall->fid,
                ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT,
                wall->tile,
                0,
                blockWorldX,
                blockWorldY,
            });
            continue;
        }

        unsigned int extendedFlags = 0;
        Proto* proto = nullptr;
        if (PID_TYPE(wall->pid) == OBJ_TYPE_WALL
            && proto_ptr(wall->pid, &proto) == 0
            && proto != nullptr) {
            extendedFlags = static_cast<unsigned int>(proto->wall.extendedFlags);
        }

        double wallWorldX;
        double wallWorldY;
        tileToWorld(wall->tile, &wallWorldX, &wallWorldY);
        const int direction = ((wall->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        walls.push_back({
            wall,
            wall->fid,
            direction,
            wall->tile,
            extendedFlags,
            wallWorldX,
            wallWorldY,
        });
    }

    // Door scenery is semantic opening data. Never synthesize a structural
    // blocker bridge through a tile occupied by a door; closed/open state will
    // be handled by the scenery renderer instead of being baked into walls.
    std::vector<int> doorTiles;
    std::vector<FirstPersonDoorSprite> doors;
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr;
         object = obj_find_next_at()) {
        if (object->tile < 0
            || FID_TYPE(object->fid) != OBJ_TYPE_SCENERY) {
            continue;
        }

        char sceneryArt[64] = { 0 };
        art_get_base_name(OBJ_TYPE_SCENERY, object->fid & 0xFFF, sceneryArt);
        const bool damagedPanel = std::strcmp(sceneryArt, "v13secr4.frm") == 0;
        if ((std::strcmp(sceneryArt, "v13secr6.frm") == 0 || damagedPanel)
            && (object->flags & OBJECT_HIDDEN) == 0
            && tile_dist(obj_dude->tile, object->tile) <= kRenderRadius) {
            double worldX;
            double worldY;
            tileToWorld(object->tile, &worldX, &worldY);
            // Verified generic structural scenery. Keep its native object and
            // blockers; only its visual representation changes here.
            doors.push_back({ object, object->fid, object->frame,
                ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT,
                object->tile, 0x08000000u, worldX, worldY, true, damagedPanel });
        }

        Proto* proto = nullptr;
        if (PID_TYPE(object->pid) == OBJ_TYPE_SCENERY
            && proto_ptr(object->pid, &proto) == 0
            && proto != nullptr
            && proto->scenery.type == SCENERY_TYPE_DOOR) {
            doorTiles.push_back(object->tile);

            if ((object->flags & OBJECT_HIDDEN) == 0
                && tile_dist(obj_dude->tile, object->tile) <= kRenderRadius) {
                double doorWorldX;
                double doorWorldY;
                tileToWorld(object->tile, &doorWorldX, &doorWorldY);
                doors.push_back({
                    object,
                    object->fid,
                    object->frame,
                    ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT,
                    object->tile,
                    static_cast<unsigned int>(proto->scenery.extendedFlags),
                    doorWorldX,
                    doorWorldY,
                });
            }
        }
    }

    auto doorAtTile = [&doorTiles](int tile) {
        return std::find(doorTiles.begin(), doorTiles.end(), tile) != doorTiles.end();
    };

    // Promote only blocker cells that form a proven bridge between visible
    // wall structure. This recovers spans such as VAULTBUR 13090 -> 13290
    // (block.frm) -> 13490 without turning every collision helper into a wall.
    //
    // A bridge is accepted only when tracing both directions on one structural
    // axis reaches compatible visible walls, passing exclusively through other
    // block.frm cells. The synthetic cell inherits art from a real wall at one
    // end, while its geometry is a normal straight structural segment.
    auto wallAtTile = [&walls](int tile) -> const FirstPersonWallSprite* {
        for (const FirstPersonWallSprite& candidate : walls) {
            if (candidate.tile == tile) {
                return &candidate;
            }
        }
        return nullptr;
    };

    auto blockAtTile = [&blockWallHints](int tile) {
        for (const FirstPersonWallSprite& block : blockWallHints) {
            if (block.tile == tile) {
                return true;
            }
        }
        return false;
    };

    auto supportsVertical = [](FirstPersonWallKind kind) {
        return kind == FIRST_PERSON_WALL_NORTH_SOUTH
            || kind == FIRST_PERSON_WALL_NORTH_CORNER
            || kind == FIRST_PERSON_WALL_SOUTH_CORNER
            || kind == FIRST_PERSON_WALL_EAST_CORNER
            || kind == FIRST_PERSON_WALL_WEST_CORNER;
    };

    auto supportsHorizontal = [](FirstPersonWallKind kind) {
        return kind == FIRST_PERSON_WALL_EAST_WEST
            || kind == FIRST_PERSON_WALL_NORTH_CORNER
            || kind == FIRST_PERSON_WALL_SOUTH_CORNER
            || kind == FIRST_PERSON_WALL_EAST_CORNER
            || kind == FIRST_PERSON_WALL_WEST_CORNER;
    };

    struct WallBridgeEnd {
        const FirstPersonWallSprite* wall;
        int steps;
    };

    auto traceBridgeEnd = [&](int startTile, int delta, bool vertical) -> WallBridgeEnd {
        int tile = startTile + delta;
        for (int step = 1; step <= 6; step++, tile += delta) {
            if (doorAtTile(tile)) {
                break;
            }
            const FirstPersonWallSprite* candidate = wallAtTile(tile);
            if (candidate != nullptr) {
                const FirstPersonWallKind kind =
                    first_person_wall_kind(candidate->extendedFlags);
                const bool compatible = vertical
                    ? supportsVertical(kind)
                    : supportsHorizontal(kind);
                return compatible
                    ? WallBridgeEnd { candidate, step }
                    : WallBridgeEnd { nullptr, step };
            }
            if (!blockAtTile(tile)) {
                break;
            }
        }
        return { nullptr, 0 };
    };

    for (const FirstPersonWallSprite& block : blockWallHints) {
        if (doorAtTile(block.tile)) {
            continue;
        }
        const WallBridgeEnd verticalA = traceBridgeEnd(block.tile, -200, true);
        const WallBridgeEnd verticalB = traceBridgeEnd(block.tile, 200, true);
        const WallBridgeEnd horizontalA = traceBridgeEnd(block.tile, -1, false);
        const WallBridgeEnd horizontalB = traceBridgeEnd(block.tile, 1, false);

        const bool verticalBridge = verticalA.wall != nullptr && verticalB.wall != nullptr;
        const bool horizontalBridge = horizontalA.wall != nullptr && horizontalB.wall != nullptr;
        if (!verticalBridge && !horizontalBridge) {
            continue;
        }

        // If a rare helper qualifies on both axes, choose the shorter proven
        // bridge instead of creating an accidental four-way wall intersection.
        bool useVertical = verticalBridge;
        if (verticalBridge && horizontalBridge) {
            useVertical = verticalA.steps + verticalB.steps
                <= horizontalA.steps + horizontalB.steps;
        }

        const WallBridgeEnd& endA = useVertical ? verticalA : horizontalA;
        const WallBridgeEnd& endB = useVertical ? verticalB : horizontalB;
        const FirstPersonWallKind preferredKind = useVertical
            ? FIRST_PERSON_WALL_NORTH_SOUTH
            : FIRST_PERSON_WALL_EAST_WEST;

        const FirstPersonWallSprite* materialSource = endA.wall;
        if (first_person_wall_kind(endB.wall->extendedFlags) == preferredKind
            && first_person_wall_kind(endA.wall->extendedFlags) != preferredKind) {
            materialSource = endB.wall;
        }

        FirstPersonWallSprite bridge = *materialSource;
        bridge.tile = block.tile;
        bridge.extendedFlags = useVertical ? 0x00000000u : 0x08000000u;
        bridge.worldX = block.worldX;
        bridge.worldY = block.worldY;
        // Keep the real wall as the pick owner; never expose the invisible
        // collision helper as an interactable rendered object.
        walls.push_back(bridge);
    }

    // Build reusable first-person materials from the original isometric wall
    // FRMs. The material owns copied pixels, so the art cache can be unlocked
    // immediately and the same material can be shared by many wall segments.
    auto getWallMaterial = [](int fid, int direction, unsigned int extendedFlags) -> FirstPersonWallMaterial* {
        for (FirstPersonWallMaterial& material : gFirstPersonWallMaterials) {
            if (material.fid == fid && material.direction == direction) {
                return &material;
            }
        }

        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(fid, &cacheEntry);
        if (art == nullptr) {
            return nullptr;
        }

        ArtFrame* frame = frame_ptr(art, 0, direction);
        unsigned char* pixels = art_frame_data(art, 0, direction);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        // Straight faces can be unsheared by column. Corner FRMs contain two
        // faces and retain the existing fallback; their arms normally borrow
        // a straight continuation material below.
        static const bool legacyMaterials = std::getenv("FALLOUT_FP_MATERIAL_LEGACY") != nullptr;
        const FirstPersonWallKind materialKind = first_person_wall_kind(extendedFlags);
        if (!legacyMaterials
            && (materialKind == FIRST_PERSON_WALL_NORTH_SOUTH
                || materialKind == FIRST_PERSON_WALL_EAST_WEST)) {
            char name[64] = { 0 };
            art_get_base_name(FID_TYPE(fid), fid & 0xFFF, name);
            auto face = first_person_rectify_columns(pixels, frame->width, frame->height,
                first_person_material_has_opening(name));
            art_ptr_unlock(cacheEntry);
            if (face.width <= 0 || face.height <= 0) {
                return nullptr;
            }
            gFirstPersonWallMaterials.push_back({ fid, direction, face.width, face.height, std::move(face.pixels) });
            return &gFirstPersonWallMaterials.back();
        }

        int opaqueMinX = frame->width;
        int opaqueMaxX = -1;
        int opaqueMinY = frame->height;
        int opaqueMaxY = -1;
        std::vector<int> rowOpaqueMinX(frame->height, frame->width);
        std::vector<int> rowOpaqueMaxX(frame->height, -1);

        for (int sy = 0; sy < frame->height; sy++) {
            for (int sx = 0; sx < frame->width; sx++) {
                if (pixels[sy * frame->width + sx] != 0) {
                    rowOpaqueMinX[sy] = std::min(rowOpaqueMinX[sy], sx);
                    rowOpaqueMaxX[sy] = std::max(rowOpaqueMaxX[sy], sx);
                    opaqueMinX = std::min(opaqueMinX, sx);
                    opaqueMaxX = std::max(opaqueMaxX, sx);
                    opaqueMinY = std::min(opaqueMinY, sy);
                    opaqueMaxY = std::max(opaqueMaxY, sy);
                }
            }
        }

        if (opaqueMaxX < opaqueMinX || opaqueMaxY < opaqueMinY) {
            art_ptr_unlock(cacheEntry);
            return nullptr;
        }

        const int materialWidth = opaqueMaxX - opaqueMinX + 1;
        const int materialHeight = opaqueMaxY - opaqueMinY + 1;
        std::vector<unsigned char> rectified(
            static_cast<size_t>(materialWidth) * materialHeight, 0);

        // Preserve source texture scale instead of stretching every scanline
        // independently. Align each row by its opaque center, then repeat only
        // its edge texels where the isometric silhouette narrows. This removes
        // the diagonal cutout while keeping bricks/panels far less warped.
        for (int my = 0; my < materialHeight; my++) {
            int sourceY = opaqueMinY + my;
            if (rowOpaqueMaxX[sourceY] < rowOpaqueMinX[sourceY]) {
                for (int radius = 1; radius < frame->height; radius++) {
                    const int up = sourceY - radius;
                    const int down = sourceY + radius;
                    if (up >= opaqueMinY
                        && rowOpaqueMaxX[up] >= rowOpaqueMinX[up]) {
                        sourceY = up;
                        break;
                    }
                    if (down <= opaqueMaxY
                        && rowOpaqueMaxX[down] >= rowOpaqueMinX[down]) {
                        sourceY = down;
                        break;
                    }
                }
            }

            const int rowMinX = rowOpaqueMinX[sourceY];
            const int rowMaxX = rowOpaqueMaxX[sourceY];
            if (rowMaxX < rowMinX) {
                continue;
            }

            const double rowCenter = (rowMinX + rowMaxX) * 0.5;
            const double outputCenter = (materialWidth - 1) * 0.5;
            for (int mx = 0; mx < materialWidth; mx++) {
                int sourceX = static_cast<int>(std::lround(
                    rowCenter + (mx - outputCenter)));
                sourceX = std::clamp(sourceX, rowMinX, rowMaxX);

                unsigned char pixel = pixels[sourceY * frame->width + sourceX];
                if (pixel == 0) {
                    for (int radius = 1; radius <= rowMaxX - rowMinX && pixel == 0; radius++) {
                        const int leftX = sourceX - radius;
                        const int rightX = sourceX + radius;
                        if (leftX >= rowMinX) {
                            pixel = pixels[sourceY * frame->width + leftX];
                        }
                        if (pixel == 0 && rightX <= rowMaxX) {
                            pixel = pixels[sourceY * frame->width + rightX];
                        }
                    }
                }

                rectified[my * materialWidth + mx] = pixel;
            }
        }

        art_ptr_unlock(cacheEntry);
        gFirstPersonWallMaterials.push_back({
            fid,
            direction,
            materialWidth,
            materialHeight,
            std::move(rectified),
        });
        return &gFirstPersonWallMaterials.back();
    };

    auto straightMaterialForCornerArm = [&](const FirstPersonWallSprite& corner,
                                           FirstPersonWallKind cornerKind,
                                           const FirstPersonWallSegment& segment)
        -> const FirstPersonWallSprite* {
        if (!first_person_wall_is_corner(cornerKind)) {
            return &corner;
        }

        const bool horizontal =
            std::abs(segment.bx - segment.ax) >= std::abs(segment.by - segment.ay);
        const int delta = first_person_corner_neighbor_delta(cornerKind, horizontal);
        if (delta == 0) {
            return &corner;
        }

        const FirstPersonWallKind desired = horizontal
            ? FIRST_PERSON_WALL_EAST_WEST
            : FIRST_PERSON_WALL_NORTH_SOUTH;

        int tile = corner.tile + delta;
        for (int step = 1; step <= 6; step++, tile += delta) {
            const FirstPersonWallSprite* candidate = wallAtTile(tile);
            if (candidate != nullptr) {
                if (first_person_wall_kind(candidate->extendedFlags) == desired) {
                    char name[64] = { 0 };
                    art_get_base_name(OBJ_TYPE_WALL, candidate->fid & 0xFFF, name);
                    // A post/window is not a solid continuation texture. Never
                    // copy its transparent opening onto an unrelated corner.
                    return first_person_material_has_opening(name) ? &corner : candidate;
                }
                // A different visible wall class is a real topology boundary;
                // don't borrow a texture through it.
                return &corner;
            }

            // Only look farther when the map explicitly supplies blocker
            // topology. This avoids stealing a material from a nearby room.
            if (!blockAtTile(tile)) {
                break;
            }
        }

        return &corner;
    };

    // Add overhead spans only for verified paired doorway art. Keep this
    // separate from walls: lintels must not become blocker reconstruction or
    // corner-material evidence. No existing segment or passage width changes.
    std::vector<FirstPersonWallSprite> renderedWalls = walls;
    for (const FirstPersonWallSprite& a : walls) {
        if (a.object->tile != a.tile) {
            continue; // Not a native wall piece (e.g. a blocker-backed bridge).
        }
        char nameA[64] = { 0 };
        art_get_base_name(OBJ_TYPE_WALL, a.fid & 0xFFF, nameA);
        if (std::strcmp(nameA, "dv1036.frm") != 0
            && std::strcmp(nameA, "dv1043.frm") != 0
            && std::strcmp(nameA, "velvdr04.frm") != 0) {
            continue;
        }
        const bool elevator = std::strcmp(nameA, "velvdr04.frm") == 0;
        const int delta = elevator ? 4 : (std::strcmp(nameA, "dv1036.frm") == 0 ? 2 : 400);
        const FirstPersonWallSprite* b = wallAtTile(a.tile + delta);
        if (b == nullptr || b->object->tile != b->tile) {
            continue;
        }
        char nameB[64] = { 0 };
        art_get_base_name(OBJ_TYPE_WALL, b->fid & 0xFFF, nameB);
        const int gap = first_person_doorway_gap(nameA, a.tile, a.extendedFlags,
            nameB, b->tile, b->extendedFlags);
        if (gap < 0) {
            continue;
        }
        const int spanCount = elevator ? 3 : 1;
        bool clear = true;
        for (int i = 0; i < spanCount; i++) {
            const int tile = gap + i;
            if (wallAtTile(tile) != nullptr || blockAtTile(tile) || doorAtTile(tile)) {
                clear = false;
            }
        }
        if (!clear) {
            continue;
        }
        for (int i = 0; i < spanCount; i++) {
            // Keep one continuous header texture across multi-cell openings.
            FirstPersonWallSprite lintel = delta == 400 ? a : *b;
            lintel.tile = gap + i;
            tileToWorld(lintel.tile, &lintel.worldX, &lintel.worldY);
            lintel.baseHeight = 1.35;
            lintel.materialVMax = 0.28;
            lintel.materialUMin = (spanCount - i - 1) / static_cast<double>(spanCount);
            lintel.materialUMax = (spanCount - i) / static_cast<double>(spanCount);
            renderedWalls.push_back(lintel);
        }
    }

    constexpr double kStructuralWallHeight = 1.65;

    // Structural geometry and wall material are deliberately separate. Straight
    // pieces keep their own art; each corner arm first tries to borrow the
    // continuation wall's clean straight material. This avoids folding one
    // isometric corner sprite around two perpendicular first-person planes.
    for (const FirstPersonWallSprite& wall : renderedWalls) {
        if (tile_dist(obj_dude->tile, wall.tile) > kRenderRadius) {
            continue;
        }
        const FirstPersonWallKind wallKind =
            first_person_wall_kind(wall.extendedFlags);
        const auto segments = first_person_wall_segments(
            wall.tile, wall.extendedFlags, wall.direction, wall.worldX, wall.worldY);

        for (const FirstPersonWallSegment& sourceSegment : segments) {
            const FirstPersonWallSprite* materialWall = &wall;
            if (!debugWalls && first_person_wall_is_corner(wallKind)) {
                materialWall =
                    straightMaterialForCornerArm(wall, wallKind, sourceSegment);
            }

            FirstPersonWallMaterial* material = nullptr;
            if (!debugWalls) {
                material = getWallMaterial(materialWall->fid, materialWall->direction, materialWall->extendedFlags);
                if (material == nullptr || material->width <= 0 || material->height <= 0) {
                    // If an adjacent borrowed material is unavailable, fall
                    // back to the corner's own art before dropping geometry.
                    material = getWallMaterial(wall.fid, wall.direction, wall.extendedFlags);
                    materialWall = &wall;
                }
                if (material == nullptr || material->width <= 0 || material->height <= 0) {
                    continue;
                }
            }

            FirstPersonWallSegment materialSegment = sourceSegment;
            if (materialWall != &wall && first_person_wall_is_corner(wallKind)) {
                // Borrowed straight art should cover the whole corner arm.
                // Preserve the arm's original texture direction so patterns do
                // not flip at the join.
                const bool reversed = sourceSegment.u1 < sourceSegment.u0;
                materialSegment.u0 = reversed ? 1.0 : 0.0;
                materialSegment.u1 = reversed ? 0.0 : 1.0;
            }

            materialSegment.u0 = wall.materialUMin
                + (wall.materialUMax - wall.materialUMin) * materialSegment.u0;
            materialSegment.u1 = wall.materialUMin
                + (wall.materialUMax - wall.materialUMin) * materialSegment.u1;

            const double joinOverlap = first_person_wall_is_corner(wallKind)
                ? 0.055
                : 0.035;
            const FirstPersonWallSegment segment =
                first_person_overlap_wall_segment(materialSegment, joinOverlap);

            const double adx = segment.ax - playerWorldX;
            const double ady = segment.ay - playerWorldY;
            const double bdx = segment.bx - playerWorldX;
            const double bdy = segment.by - playerWorldY;
            double ax = adx * rightX + ady * rightY;
            double az = adx * forwardX + ady * forwardY;
            double bx = bdx * rightX + bdy * rightY;
            double bz = bdx * forwardX + bdy * forwardY;

            double u0 = segment.u0;
            double u1 = segment.u1;
            if (!first_person_clip_wall(ax, az, bx, bz, u0, u1, kNearPlane)) {
                continue;
            }

            const int screenAX = width / 2 + static_cast<int>(ax * focal / az);
            const int screenBX = width / 2 + static_cast<int>(bx * focal / bz);
            const int groundAY = horizon + static_cast<int>(focal * kEyeHeight / az);
            const int groundBY = horizon + static_cast<int>(focal * kEyeHeight / bz);
            const int bottomAY = groundAY - static_cast<int>(focal * wall.baseHeight / az);
            const int bottomBY = groundBY - static_cast<int>(focal * wall.baseHeight / bz);
            const int topAY = groundAY - static_cast<int>(focal * kStructuralWallHeight / az);
            const int topBY = groundBY - static_cast<int>(focal * kStructuralWallHeight / bz);

            const int minX = std::max(0, std::min(screenAX, screenBX));
            const int maxX = std::min(width - 1, std::max(screenAX, screenBX));
            if (minX > maxX) {
                continue;
            }

            const double screenSpan = static_cast<double>(screenBX - screenAX);
            if (std::abs(screenSpan) < 1.0) {
                continue;
            }

            if (gpuWorld) {
                const FirstPersonGpuVertex vertices[4] = {
                    gpuVertex(screenAX, topAY, az, u0, 0.0),
                    gpuVertex(screenBX, topBY, bz, u1, 0.0),
                    gpuVertex(screenBX, bottomBY, bz, u1, wall.materialVMax),
                    gpuVertex(screenAX, bottomAY, az, u0, wall.materialVMax),
                };
                first_person_world_gpu_quad(material->pixels.data(), material->width,
                    material->height, vertices, gpuOwner(wall.object));
                continue;
            }

            const double invAz = 1.0 / az;
            const double invBz = 1.0 / bz;
            for (int screenX = minX; screenX <= maxX; screenX++) {
                const double s = (screenX - screenAX) / screenSpan;
                if (s < 0.0 || s > 1.0) {
                    continue;
                }

                const double invZ = invAz + (invBz - invAz) * s;
                if (invZ <= 0.0) {
                    continue;
                }

                const double z = 1.0 / invZ;
                const double worldT =
                    ((1.0 - s) * u0 * invAz + s * u1 * invBz) / invZ;
                const int bottom = static_cast<int>(
                    bottomAY + (bottomBY - bottomAY) * s);
                const int top = static_cast<int>(
                    topAY + (topBY - topAY) * s);
                const int columnHeight = bottom - top;
                if (columnHeight <= 0) {
                    continue;
                }

                for (int screenY = std::max(0, top);
                     screenY <= std::min(height - 1, bottom);
                     screenY++) {
                    unsigned char pixel;
                    if (debugWalls) {
                        pixel = colorTable[debugWallColor(wallKind)];
                    } else {
                        const double materialU = std::clamp(worldT, 0.0, 1.0);
                        const int materialX = std::clamp(
                            static_cast<int>(std::lround(
                                materialU * std::max(0, material->width - 1))),
                            0,
                            std::max(0, material->width - 1));
                        const int materialY = std::clamp(
                            (screenY - top) * std::max(1, static_cast<int>(
                                material->height * wall.materialVMax)) / columnHeight,
                            0,
                            std::max(0, material->height - 1));
                        pixel = material->pixels[
                            materialY * material->width + materialX];
                        if (pixel == 0) {
                            continue;
                        }
                    }

                    const int destination = screenY * width + screenX;
                    if (z < depthBuffer[destination]) {
                        buffer[destination] = pixel;
                        depthBuffer[destination] = z;
                        gFirstPersonPicks[destination] = {
                            wall.object,
                            wall.object->id,
                        };
                    }
                }
            }
        }
    }

    // First-person doors are structural scenery. They occupy the same wall
    // lattice as the opening and participate in depth/pick testing, using
    // their closed frame as the panel. Native collision/use logic remains
    // authoritative; only the panel's visible extent follows the animation.
    constexpr double kDoorHeight = 1.55;
    for (const FirstPersonDoorSprite& door : doors) {
        // Native doors open by animating away from frame 0. Slide the closed
        // panel into its jamb by that progress instead of switching to the
        // isometric animation frames.
        double openness = 0.0;
        if (!door.portal && door.frame != 0) {
            CacheEntry* doorEntry = nullptr;
            if (Art* doorArt = art_ptr_lock(door.fid, &doorEntry)) {
                openness = first_person_door_openness(door.frame, art_frame_max_frame(doorArt));
                art_ptr_unlock(doorEntry);
            }
        }

        FirstPersonWallMaterial portalMaterial {};
        FirstPersonWallMaterial* material = nullptr;
        if (door.portal) {
            CacheEntry* entry = nullptr;
            Art* art = art_ptr_lock(door.fid, &entry);
            if (art == nullptr) {
                continue;
            }
            const int number = std::clamp(door.frame, 0, std::max(0, art_frame_max_frame(art) - 1));
            ArtFrame* frame = frame_ptr(art, number, door.direction);
            unsigned char* source = art_frame_data(art, number, door.direction);
            if (frame != nullptr && source != nullptr && frame->width > 0 && frame->height > 0) {
                portalMaterial.width = door.damagedPanel
                    ? std::max(1, frame->width * 302 / 342) : frame->width;
                portalMaterial.height = std::max(1, door.damagedPanel
                    ? frame->height * 105 / 223 : frame->height * 112 / 142);
                portalMaterial.pixels.resize(static_cast<size_t>(portalMaterial.width)
                    * portalMaterial.height, 0);
                // Asset-specific front-face profiles from the exported native art.
                // The damaged 342x223 panel includes a side, top caps and
                // ground rubble: crop to x=40..341, y=91..28 + 105 rows.
                // Preserve index-zero damage. Do not flatten every opaque
                // column: that would drag broken edges down into the hole.
                // The arch keeps its established 151x142 profile.
                for (int x = 0; x < portalMaterial.width; x++) {
                    const double u = x / static_cast<double>(std::max(1, portalMaterial.width - 1));
                    const double top = door.damagedPanel
                        ? frame->height * ((91.0 - 63.0 * u) / 223.0)
                        : frame->height * (36.0 / 142.0) * (1.0 - u);
                    const int sx = door.damagedPanel
                        ? std::clamp(static_cast<int>(std::lround(
                            frame->width * (40.0 + 301.0 * u) / 342.0)),
                            0, frame->width - 1) : x;
                    for (int y = 0; y < portalMaterial.height; y++) {
                        const int sy = static_cast<int>(std::lround(top + y));
                        if (sy >= 0 && sy < frame->height) {
                            portalMaterial.pixels[y * portalMaterial.width + x] =
                                source[sy * frame->width + sx];
                        }
                    }
                }
                material = &portalMaterial;
            }
            art_ptr_unlock(entry);
        } else {
            material = getWallMaterial(door.fid, door.direction, door.extendedFlags);
        }
        if (material == nullptr || material->width <= 0 || material->height <= 0) {
            continue;
        }

        auto doorSegments = first_person_wall_segments(
            door.tile,
            door.extendedFlags,
            door.direction,
            door.worldX,
            door.worldY);

        if (door.portal) {
            // Preserve the existing parity-corrected wall line. Enlarge only
            // this scenery plane. The arch spans five columns; the damaged
            // section spans eight intervals between its surrounding wall lines.
            for (FirstPersonWallSegment& segment : doorSegments) {
                const double centerX = (segment.ax + segment.bx) * 0.5;
                const double columns = door.damagedPanel ? 8.0 : 5.0;
                segment.ax = centerX + (segment.ax - centerX) * columns;
                segment.bx = centerX + (segment.bx - centerX) * columns;
            }
        }
        const double planeHeight = door.portal ? kStructuralWallHeight : kDoorHeight;
        if (door.portal && !doorSegments.empty()) {
            // Keep the verified central lattice plane, with two visual faces
            // and narrow returns. No native footprint or collision changes.
            constexpr double halfDepth = 0.06;
            const FirstPersonWallSegment center = doorSegments.front();
            const double dx = center.bx - center.ax;
            const double dy = center.by - center.ay;
            const double length = std::hypot(dx, dy);
            if (length > 0.0) {
                const double nx = -dy / length * halfDepth;
                const double ny = dx / length * halfDepth;
                doorSegments.clear();
                doorSegments.push_back({ center.ax + nx, center.ay + ny,
                    center.bx + nx, center.by + ny, 0.0, 1.0 });
                doorSegments.push_back({ center.ax - nx, center.ay - ny,
                    center.bx - nx, center.by - ny, 0.0, 1.0 });
                auto addReturn = [&](double u, double sampleU) {
                    const double x = center.ax + dx * u;
                    const double y = center.ay + dy * u;
                    doorSegments.push_back({ x - nx, y - ny, x + nx, y + ny, sampleU, sampleU });
                };
                addReturn(0.0, 0.0);
                addReturn(1.0, 1.0);
                // Locate the central opening at lower-post height. Sample the
                // adjacent solid column for each inner jamb return. Preserve
                // alpha up the column so arched trim is not turned into a box.
                const int row = material->height * 4 / 5;
                const int middle = material->width / 2;
                if (!door.damagedPanel && material->width > 2
                    && material->pixels[row * material->width + middle] == 0) {
                    int left = middle;
                    int right = middle;
                    while (left > 0 && material->pixels[row * material->width + left - 1] == 0) left--;
                    while (right + 1 < material->width && material->pixels[row * material->width + right + 1] == 0) right++;
                    const double scale = 1.0 / (material->width - 1);
                    if (left > 0) addReturn(left * scale, (left - 1) * scale);
                    if (right + 1 < material->width) addReturn(right * scale, (right + 1) * scale);
                }
            }
        }

        for (FirstPersonWallSegment sourceSegment : doorSegments) {
            // Portal face/return UVs are already defined; native door faces
            // retain their full-width mapping and existing seam overlap.
            if (!door.portal) {
                sourceSegment.u0 = 0.0;
                sourceSegment.u1 = 1.0;
            }
            const FirstPersonWallSegment fullSegment = door.portal ? sourceSegment
                : first_person_overlap_wall_segment(sourceSegment, 0.02);
            // Pass 0 draws the visible panel. Pass 1 keeps a moving or open
            // door targetable over its whole doorway, so it can be closed.
            for (int pass = 0; pass < 2; pass++) {
                const bool proxyPass = pass == 1;
                if (proxyPass ? openness <= 0.0 : openness >= 1.0) {
                    continue;
                }
                const FirstPersonWallSegment segment = proxyPass ? fullSegment
                    : first_person_slide_door_segment(fullSegment, openness);

                const double adx = segment.ax - playerWorldX;
                const double ady = segment.ay - playerWorldY;
                const double bdx = segment.bx - playerWorldX;
                const double bdy = segment.by - playerWorldY;
                double ax = adx * rightX + ady * rightY;
                double az = adx * forwardX + ady * forwardY;
                double bx = bdx * rightX + bdy * rightY;
                double bz = bdx * forwardX + bdy * forwardY;

                double u0 = segment.u0;
                double u1 = segment.u1;
                if (!first_person_clip_wall(ax, az, bx, bz, u0, u1, kNearPlane)) {
                    continue;
                }

                const int screenAX = width / 2 + static_cast<int>(ax * focal / az);
                const int screenBX = width / 2 + static_cast<int>(bx * focal / bz);
                const int bottomAY = horizon + static_cast<int>(focal * kEyeHeight / az);
                const int bottomBY = horizon + static_cast<int>(focal * kEyeHeight / bz);
                const int topAY = bottomAY - static_cast<int>(focal * planeHeight / az);
                const int topBY = bottomBY - static_cast<int>(focal * planeHeight / bz);

                const int minX = std::max(0, std::min(screenAX, screenBX));
                const int maxX = std::min(width - 1, std::max(screenAX, screenBX));
                const double screenSpan = static_cast<double>(screenBX - screenAX);
                if (minX > maxX || std::abs(screenSpan) < 1.0) {
                    continue;
                }

                if (gpuWorld) {
                    const FirstPersonGpuVertex vertices[4] = {
                        gpuVertex(screenAX, topAY, az, u0, 0.0),
                        gpuVertex(screenBX, topBY, bz, u1, 0.0),
                        gpuVertex(screenBX, bottomBY, bz, u1, 1.0),
                        gpuVertex(screenAX, bottomAY, az, u0, 1.0),
                    };
                    if (proxyPass) {
                        first_person_world_gpu_quad(nullptr, 0, 0, vertices, gpuOwner(door.object), true);
                    } else {
                        first_person_world_gpu_quad(material->pixels.data(), material->width,
                            material->height, vertices, gpuOwner(door.object));
                    }
                    continue;
                }

                const double invAz = 1.0 / az;
                const double invBz = 1.0 / bz;
                for (int screenX = minX; screenX <= maxX; screenX++) {
                    const double s = (screenX - screenAX) / screenSpan;
                    if (s < 0.0 || s > 1.0) {
                        continue;
                    }

                    const double invZ = invAz + (invBz - invAz) * s;
                    if (invZ <= 0.0) {
                        continue;
                    }

                    const double z = 1.0 / invZ;
                    const double worldT =
                        ((1.0 - s) * u0 * invAz + s * u1 * invBz) / invZ;
                    const int bottom = static_cast<int>(
                        bottomAY + (bottomBY - bottomAY) * s);
                    const int top = static_cast<int>(
                        topAY + (topBY - topAY) * s);
                    const int columnHeight = bottom - top;
                    if (columnHeight <= 0) {
                        continue;
                    }

                    const int materialX = std::clamp(
                        static_cast<int>(std::lround(
                            std::clamp(worldT, 0.0, 1.0)
                            * std::max(0, material->width - 1))),
                        0,
                        std::max(0, material->width - 1));

                    for (int screenY = std::max(0, top);
                         screenY <= std::min(height - 1, bottom);
                         screenY++) {
                        const int materialY = std::clamp(
                            (screenY - top) * material->height / columnHeight,
                            0,
                            std::max(0, material->height - 1));
                        const int destination = screenY * width + screenX;
                        if (proxyPass) {
                            if (z <= depthBuffer[destination] + 0.0001
                                && z < interactionDepth[destination]) {
                                interactionDepth[destination] = z;
                                gFirstPersonInteractionPicks[destination] = {
                                    door.object,
                                    door.object->id,
                                };
                            }
                            continue;
                        }

                        const unsigned char pixel =
                            material->pixels[materialY * material->width + materialX];
                        if (pixel == 0) {
                            continue;
                        }

                        if (z < depthBuffer[destination]) {
                            buffer[destination] = pixel;
                            depthBuffer[destination] = z;
                            gFirstPersonPicks[destination] = {
                                door.object,
                                door.object->id,
                            };
                        }
                    }
                }
            }
        }
    }

    // v0.011: expose the rest of Fallout's map objects in first person.
    // This is intentionally a billboard pass for now: scenery, critters,
    // items and misc objects use their live FRM/frame/rotation, anchored to
    // their real map hex. It gives us a much more complete scene while the
    // wall reconstruction work remains independent.
    std::vector<FirstPersonObjectSprite> objectSprites;
    for (Object* object = obj_find_first_at(map_elevation);
         object != nullptr;
         object = obj_find_next_at()) {
        if (object == obj_dude
            || object->tile < 0
            || (object->flags & OBJECT_HIDDEN) != 0
            || tile_dist(obj_dude->tile, object->tile) > kRenderRadius) {
            continue;
        }

        const int type = FID_TYPE(object->fid);
        if (type != OBJ_TYPE_ITEM
            && type != OBJ_TYPE_CRITTER
            && type != OBJ_TYPE_SCENERY
            && type != OBJ_TYPE_MISC) {
            continue;
        }

        if (type == OBJ_TYPE_SCENERY) {
            // Native invisible collision helpers are not visible scenery. The
            // wall collector already treats this art as topology-only; do the
            // same here before it can write pixels, depth, or pick IDs.
            char artName[64] = { 0 };
            if (art_get_base_name(type, object->fid & 0xFFF, artName) == 0
                && (std::strcmp(artName, "block.frm") == 0
                    || std::strcmp(artName, "v13secr6.frm") == 0
                    || std::strcmp(artName, "v13secr4.frm") == 0)) {
                continue;
            }
            Proto* sceneryProto = nullptr;
            if (PID_TYPE(object->pid) == OBJ_TYPE_SCENERY
                && proto_ptr(object->pid, &sceneryProto) == 0
                && sceneryProto != nullptr
                && sceneryProto->scenery.type == SCENERY_TYPE_DOOR) {
                // Doors, closed, moving or open, are drawn by the structural
                // door pass above as a sliding panel.
                continue;
            }
        }

        double objectWorldX;
        double objectWorldY;
        tileToWorld(object->tile, &objectWorldX, &objectWorldY);
        if (type == OBJ_TYPE_CRITTER && (object->x != 0 || object->y != 0)) {
            // Native walking moves a critter between hexes through its x/y
            // screen shift before it hops to the next tile.
            double shiftX;
            double shiftY;
            first_person_iso_to_world(object->x, object->y, &shiftX, &shiftY);
            objectWorldX += shiftX;
            objectWorldY += shiftY;
        }
        const double dx = objectWorldX - playerWorldX;
        const double dy = objectWorldY - playerWorldY;
        const double cameraX = dx * rightX + dy * rightY;
        const double cameraZ = dx * forwardX + dy * forwardY;
        if (cameraZ < kNearPlane || cameraZ > kFarPlane) {
            continue;
        }

        int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
        if (type == OBJ_TYPE_CRITTER) {
            // Show the side of the critter that faces the viewer: its FRM
            // directions are drawn for the fixed isometric camera.
            double directionAngles[ROTATION_COUNT];
            bool haveAngles = true;
            for (int r = 0; r < ROTATION_COUNT && haveAngles; r++) {
                const int neighbor = tile_num_in_direction(object->tile, r, 1);
                double neighborX;
                double neighborY;
                haveAngles = neighbor >= 0 && neighbor != object->tile;
                if (haveAngles) {
                    tileToWorld(neighbor, &neighborX, &neighborY);
                    directionAngles[r] = std::atan2(neighborY - objectWorldY, neighborX - objectWorldX);
                }
            }
            if (haveAngles && (dx != 0.0 || dy != 0.0)) {
                direction = first_person_billboard_direction(directionAngles, std::atan2(dy, dx), direction);
            }
        }
        objectSprites.push_back({ object, object->fid, object->frame, direction, type, cameraX, cameraZ });
    }

    std::sort(objectSprites.begin(), objectSprites.end(), [](const FirstPersonObjectSprite& a, const FirstPersonObjectSprite& b) {
        return a.z > b.z;
    });

    std::vector<FirstPersonRenderedBounds> renderedObjectBounds;
    renderedObjectBounds.reserve(objectSprites.size());

    for (const FirstPersonObjectSprite& object : objectSprites) {
        CacheEntry* cacheEntry = nullptr;
        Art* art = art_ptr_lock(object.fid, &cacheEntry);
        if (art == nullptr) {
            continue;
        }

        const int maxFrame = art_frame_max_frame(art);
        const int frameNumber = std::clamp(object.frame, 0, std::max(0, maxFrame - 1));
        ArtFrame* frame = frame_ptr(art, frameNumber, object.direction);
        unsigned char* pixels = art_frame_data(art, frameNumber, object.direction);
        if (frame == nullptr || pixels == nullptr || frame->width <= 0 || frame->height <= 0) {
            art_ptr_unlock(cacheEntry);
            continue;
        }

        // Preserve each sprite's original aspect ratio. These dimensions are
        // deliberately approximate; the purpose of this pass is to expose the
        // real map contents, not pretend that the 2D art is finished geometry.
        double worldHeight;
        switch (object.type) {
        case OBJ_TYPE_CRITTER:
            // Use one pixel scale for all live animation/death frames. A
            // fixed projected height enlarged rats to human size and inflated
            // short, wide corpse frames even further.
            worldHeight = frame->height / 80.0;
            break;
        case OBJ_TYPE_ITEM:
            worldHeight = std::clamp(frame->height / 80.0, 0.18, 0.70);
            break;
        case OBJ_TYPE_SCENERY:
            worldHeight = std::clamp(frame->height / 72.0, 0.45, 2.40);
            break;
        default:
            worldHeight = std::clamp(frame->height / 80.0, 0.25, 1.60);
            break;
        }

        int opaqueMinX = frame->width;
        int opaqueMinY = frame->height;
        int opaqueMaxX = -1;
        int opaqueMaxY = -1;
        for (int sy = 0; sy < frame->height; sy++) {
            for (int sx = 0; sx < frame->width; sx++) {
                if (pixels[sy * frame->width + sx] != 0) {
                    opaqueMinX = std::min(opaqueMinX, sx);
                    opaqueMinY = std::min(opaqueMinY, sy);
                    opaqueMaxX = std::max(opaqueMaxX, sx);
                    opaqueMaxY = std::max(opaqueMaxY, sy);
                }
            }
        }

        const double worldWidth = worldHeight * frame->width / frame->height;
        const int projectedWidth = std::max(1, static_cast<int>(focal * worldWidth / object.z));
        const int projectedHeight = std::max(1, static_cast<int>(focal * worldHeight / object.z));
        const int centerX = width / 2 + static_cast<int>(object.x * focal / object.z);
        // Project the actual ground anchor, even when it is below the viewport.
        // Clipping belongs to the draw bounds, not to object placement: pinning
        // this baseline to the screen made nearby scenery/corpses float upward.
        int bottom = horizon + static_cast<int>(focal * kEyeHeight / object.z);
        int left = centerX - projectedWidth / 2;
        const double pixelScale = static_cast<double>(projectedHeight) / frame->height;
        if (object.type == OBJ_TYPE_CRITTER) {
            // The native anchor: the frame pixel that lands on the hex is
            // offset from the frame's bottom centre by the direction's offsets.
            left += static_cast<int>(std::lround(art->xOffsets[object.direction] * pixelScale));
            bottom += static_cast<int>(std::lround(art->yOffsets[object.direction] * pixelScale));
        } else if (object.type == OBJ_TYPE_ITEM && opaqueMaxY >= 0) {
            // Items rest their lowest visible pixel on the floor.
            bottom += static_cast<int>(std::lround((frame->height - 1 - opaqueMaxY) * pixelScale));
        }
        const int top = bottom - projectedHeight;
        renderedObjectBounds.push_back({
            object.object,
            object.object->id,
            std::max(0, left),
            std::max(0, top),
            std::min(width - 1, left + projectedWidth - 1),
            std::min(height - 1, bottom),
        });

        if (gpuWorld) {
            const std::uint32_t owner = gpuOwner(object.object);
            const FirstPersonGpuVertex vertices[4] = {
                gpuVertex(left, top, object.z, 0.0, 0.0),
                gpuVertex(left + projectedWidth, top, object.z, 1.0, 0.0),
                gpuVertex(left + projectedWidth, bottom, object.z, 1.0, 1.0),
                gpuVertex(left, bottom, object.z, 0.0, 1.0),
            };
            first_person_world_gpu_quad(pixels, frame->width, frame->height, vertices, owner);
            if (opaqueMaxX >= opaqueMinX && opaqueMaxY >= opaqueMinY) {
                const double x0 = left + projectedWidth * (opaqueMinX / static_cast<double>(frame->width));
                const double x1 = left + projectedWidth * ((opaqueMaxX + 1.0) / frame->width);
                const double y0 = top + projectedHeight * (opaqueMinY / static_cast<double>(frame->height));
                const double y1 = top + projectedHeight * ((opaqueMaxY + 1.0) / frame->height);
                const FirstPersonGpuVertex proxy[4] = {
                    gpuVertex(x0, y0, object.z, 0.0, 0.0), gpuVertex(x1, y0, object.z, 1.0, 0.0),
                    gpuVertex(x1, y1, object.z, 1.0, 1.0), gpuVertex(x0, y1, object.z, 0.0, 1.0),
                };
                first_person_world_gpu_quad(nullptr, 0, 0, proxy, owner, true);
            }
            art_ptr_unlock(cacheEntry);
            continue;
        }

        for (int screenY = std::max(0, top); screenY <= std::min(height - 1, bottom); screenY++) {
            const int sourceY = std::clamp((screenY - top) * frame->height / projectedHeight, 0, frame->height - 1);
            for (int screenX = std::max(0, left); screenX < std::min(width, left + projectedWidth); screenX++) {
                const int sourceX = std::clamp((screenX - left) * frame->width / projectedWidth, 0, frame->width - 1);
                const int destination = screenY * width + screenX;

                // Interaction footprint: use the source sprite's opaque bounds,
                // but still honor current scene depth. This fills transparent
                // holes inside a terminal/container/critter silhouette without
                // making the whole projected FRM rectangle clickable.
                if (opaqueMaxX >= opaqueMinX && opaqueMaxY >= opaqueMinY
                    && sourceX >= opaqueMinX && sourceX <= opaqueMaxX
                    && sourceY >= opaqueMinY && sourceY <= opaqueMaxY
                    && object.z <= depthBuffer[destination] + 0.0001
                    && object.z < interactionDepth[destination]) {
                    interactionDepth[destination] = object.z;
                    gFirstPersonInteractionPicks[destination] = {
                        object.object,
                        object.object->id,
                    };
                }

                const unsigned char pixel = pixels[sourceY * frame->width + sourceX];
                if (pixel != 0 && object.z < depthBuffer[destination]) {
                    buffer[destination] = pixel;
                    depthBuffer[destination] = object.z;
                    gFirstPersonPicks[destination] = { object.object, object.object->id };
                }
            }
        }

        art_ptr_unlock(cacheEntry);
    }

    if (gpuWorld) {
        static std::vector<unsigned char> gpuPixels;
        static std::vector<float> gpuDepth;
        static std::vector<std::uint32_t> gpuIds, gpuProxies;
        gpuPixels.resize(pixelCount); gpuDepth.resize(pixelCount);
        gpuIds.resize(pixelCount); gpuProxies.resize(pixelCount);
        if (!first_person_world_gpu_read(gpuPixels.data(), gpuDepth.data(), gpuIds.data(), gpuProxies.data())) {
            // The backend disables itself before returning. Re-render once
            // using the established software path instead of showing a partial frame.
            first_person_render_now();
            return;
        }
        constexpr double n = 0.45, f = 128.0;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                const int dst = y * width + x, src = (height - 1 - y) * width + x;
                buffer[dst] = gpuPixels[src];
                depthBuffer[dst] = gpuDepth[src] >= 1.0f ? 1.0e30
                    : 2.0 * f * n / (f + n - (2.0 * gpuDepth[src] - 1.0) * (f - n));
                if (gpuIds[src] < gpuOwners.size()) gFirstPersonPicks[dst] = gpuOwners[gpuIds[src]];
                if (gpuProxies[src] < gpuOwners.size()) gFirstPersonInteractionPicks[dst] = gpuOwners[gpuProxies[src]];
            }
        }
    }

    if (gpuHexVisible) {
        const unsigned char highlight = colorTable[31744];
        for (int edge = 0; edge < 6; edge++) {
            const int next = (edge + 1) % 6;
            const int steps = std::max(std::abs(gpuHexX[next] - gpuHexX[edge]),
                std::abs(gpuHexY[next] - gpuHexY[edge]));
            for (int step = 0; step <= steps; step++) {
                const double t = steps > 0 ? step / static_cast<double>(steps) : 0.0;
                const int x = static_cast<int>(std::lround(gpuHexX[edge] + t * (gpuHexX[next] - gpuHexX[edge])));
                const int y = static_cast<int>(std::lround(gpuHexY[edge] + t * (gpuHexY[next] - gpuHexY[edge])));
                if (y <= horizon || x < 0 || x >= width || y >= height) continue;
                const double z = focal * kEyeHeight / (y - horizon);
                if (z <= depthBuffer[y * width + x] + 0.05) buffer[y * width + x] = highlight;
            }
        }
    }

    Object* firstPersonHoverObject = nullptr;
    const int hoverX = width / 2;
    const int hoverY = height / 2;
    first_person_heap_check(gpuWorld ? "fp-world-gpu" : "fp-world-software");

    // Native Fallout outlines are painted by the isometric world renderer, so
    // they are not visible when the first-person scene replaces that renderer.
    // Give the pick buffer its own lightweight hover feedback instead. This
    // uses the same small assist radius as first_person_object_at, highlights
    // the actual visible pixels of the selected live object, and intentionally
    // ignores walls so a room surface cannot steal interaction feedback from a
    // nearby critter, item or scenery object.
    if (hoverX >= 0 && hoverX < width && hoverY >= 0 && hoverY < height) {
        constexpr int kPickAssistRadius = 6;
        FirstPersonPick hoverPick { nullptr, -1 };
        int bestDistanceSquared = kPickAssistRadius * kPickAssistRadius + 1;

        for (int dy = -kPickAssistRadius; dy <= kPickAssistRadius; dy++) {
            const int py = hoverY + dy;
            if (py < 0 || py >= height) {
                continue;
            }

            for (int dx = -kPickAssistRadius; dx <= kPickAssistRadius; dx++) {
                const int distanceSquared = dx * dx + dy * dy;
                if (distanceSquared > kPickAssistRadius * kPickAssistRadius
                    || distanceSquared >= bestDistanceSquared) {
                    continue;
                }

                const int px = hoverX + dx;
                if (px < 0 || px >= width) {
                    continue;
                }

                FirstPersonPick candidate =
                    gFirstPersonPicks[py * width + px];
                if (candidate.object == nullptr) {
                    candidate = gFirstPersonInteractionPicks[py * width + px];
                }
                if (candidate.object == nullptr
                    || FID_TYPE(candidate.object->fid) == OBJ_TYPE_WALL) {
                    continue;
                }

                hoverPick = candidate;
                bestDistanceSquared = distanceSquared;
            }
        }

        if (hoverPick.object != nullptr) {
            firstPersonHoverObject = hoverPick.object;
            int minPickX = width;
            int minPickY = height;
            int maxPickX = -1;
            int maxPickY = -1;
            for (const FirstPersonRenderedBounds& bounds : renderedObjectBounds) {
                if (bounds.object == hoverPick.object && bounds.id == hoverPick.id) {
                    minPickX = bounds.left;
                    minPickY = bounds.top;
                    maxPickX = bounds.right;
                    maxPickY = bounds.bottom;
                    break;
                }
            }

            if (maxPickX >= minPickX && maxPickY >= minPickY) {
                const bool attackTarget =
                    first_person_mode() == GAME_MOUSE_MODE_CROSSHAIR
                    && FID_TYPE(hoverPick.object->fid) == OBJ_TYPE_CRITTER
                    && hoverPick.object != obj_dude;
                const int highlightColor =
                    attackTarget ? colorTable[992] : colorTable[31744];

                // In attack mode, translate the original visible critter sprite
                // into a lightweight Fallout-green targeting treatment. Only
                // pixels that actually belong to this depth-tested critter are
                // touched, so walls/scenery in front continue to occlude it.
                // A checker pattern leaves enough of the source art visible to
                // retain the original sprite identity.
                if (attackTarget) {
                    for (int py = minPickY; py <= maxPickY; py++) {
                        for (int px = minPickX; px <= maxPickX; px++) {
                            const FirstPersonPick pick =
                                gFirstPersonPicks[py * width + px];
                            if (pick.object == hoverPick.object
                                && pick.id == hoverPick.id
                                && ((px + py) & 1) == 0) {
                                buffer[py * width + px] = highlightColor;
                            }
                        }
                    }
                }

                constexpr int kPad = 2;
                constexpr int kCorner = 5;
                const int left = std::max(0, minPickX - kPad);
                const int right = std::min(width - 1, maxPickX + kPad);
                const int top = std::max(0, minPickY - kPad);
                const int bottom = std::min(height - 1, maxPickY + kPad);

                auto putHighlight = [&](int x, int y) {
                    if (x >= 0 && x < width && y >= 0 && y < height) {
                        buffer[y * width + x] = highlightColor;
                    }
                };

                for (int i = 0; i < kCorner; i++) {
                    putHighlight(left + i, top);
                    putHighlight(left, top + i);
                    putHighlight(right - i, top);
                    putHighlight(right, top + i);
                    putHighlight(left + i, bottom);
                    putHighlight(left, bottom - i);
                    putHighlight(right - i, bottom);
                    putHighlight(right, bottom - i);
                }

                if (attackTarget) {
                    int hitMode = 0;
                    bool aiming = false;
                    char targetLabel[96];
                    const char* name = critter_name(hoverPick.object);
                    if (name == nullptr || *name == '\0') {
                        name = "CRITTER";
                    }

                    if (intface_get_attack(&hitMode, &aiming) == 0) {
                        const int badShot = combat_check_bad_shot(
                            obj_dude,
                            hoverPick.object,
                            hitMode,
                            aiming,
                            isInCombat());
                        if (badShot == COMBAT_BAD_SHOT_OK) {
                            const int accuracy = determine_to_hit(
                                obj_dude,
                                hoverPick.object,
                                HIT_LOCATION_UNCALLED,
                                hitMode);
                            std::snprintf(
                                targetLabel,
                                sizeof(targetLabel),
                                "%s  %d%%",
                                name,
                                std::clamp(accuracy, 0, 95));
                        } else {
                            const char* reason = "BLOCKED";
                            switch (badShot) {
                            case COMBAT_BAD_SHOT_NO_AMMO:
                                reason = "NO AMMO";
                                break;
                            case COMBAT_BAD_SHOT_OUT_OF_RANGE:
                                reason = "OUT OF RANGE";
                                break;
                            case COMBAT_BAD_SHOT_NOT_ENOUGH_AP:
                                reason = "NO AP";
                                break;
                            case COMBAT_BAD_SHOT_ALREADY_DEAD:
                                reason = "DEAD";
                                break;
                            case COMBAT_BAD_SHOT_AIM_BLOCKED:
                                reason = "BLOCKED";
                                break;
                            case COMBAT_BAD_SHOT_ARM_CRIPPLED:
                                reason = "ARM CRIPPLED";
                                break;
                            case COMBAT_BAD_SHOT_BOTH_ARMS_CRIPPLED:
                                reason = "ARMS CRIPPLED";
                                break;
                            default:
                                break;
                            }
                            std::snprintf(
                                targetLabel,
                                sizeof(targetLabel),
                                "%s  %s",
                                name,
                                reason);
                        }
                    } else {
                        std::snprintf(
                            targetLabel,
                            sizeof(targetLabel),
                            "%s",
                            name);
                    }

                    const int oldFont = text_curr();
                    text_font(101);
                    const int labelPadding = 2;
                    const int labelWidth = std::min(
                        width,
                        text_width(targetLabel) + labelPadding * 2);
                    const int labelHeight = text_height() + labelPadding * 2;
                    const int labelX = std::clamp(
                        (left + right - labelWidth) / 2,
                        0,
                        std::max(0, width - labelWidth));
                    const int labelY = std::max(0, top - labelHeight - 2);

                    if (labelWidth > 0 && labelHeight > 0) {
                        buf_fill(
                            buffer + labelY * width + labelX,
                            labelWidth,
                            labelHeight,
                            width,
                            colorTable[0]);
                        text_to_buf(
                            buffer + (labelY + labelPadding) * width
                                + labelX + labelPadding,
                            targetLabel,
                            std::max(0, labelWidth - labelPadding * 2),
                            width,
                            highlightColor);
                    }
                    text_font(oldFont);
                }
            }
        }
    }

    // First-person weapon presentation. Fallout decides which weapon is
    // equipped and what the player is doing; this layer only decides how that
    // native state should be framed in first person.
    {
        Object* heldItem = nullptr;
        if (intface_get_current_item(&heldItem) == 0
            && heldItem != nullptr
            && item_get_type(heldItem) == ITEM_TYPE_WEAPON) {
            int hitMode = 0;
            bool aiming = false;
            if (intface_get_attack(&hitMode, &aiming) != 0) {
                hitMode = -1;
            }

            const FirstPersonWeaponPose pose = first_person_weapon_pose(hitMode);
            const int weaponAnimationCode = item_w_anim_code(heldItem);
            const FirstPersonWeaponProfile profile =
                first_person_weapon_profile(weaponAnimationCode);

            double widthFraction = profile.loweredWidth;
            double bottomFraction = profile.loweredBottom;
            switch (pose) {
            case FirstPersonWeaponPose::Ready:
                widthFraction = profile.readyWidth;
                bottomFraction = profile.readyBottom;
                break;
            case FirstPersonWeaponPose::Attack:
                widthFraction = profile.attackWidth;
                bottomFraction = profile.attackBottom;
                break;
            case FirstPersonWeaponPose::Reload:
                widthFraction = profile.reloadWidth;
                bottomFraction = profile.reloadBottom;
                break;
            case FirstPersonWeaponPose::Lowered:
            default:
                break;
            }

            first_person_heap_check("fp-weapon-before");
            const int inventoryFid = item_inv_fid(heldItem);
            if (inventoryFid >= 0 && art_exists(inventoryFid)) {
                CacheEntry* weaponArtKey = nullptr;
                Art* weaponArt = art_ptr_lock(inventoryFid, &weaponArtKey);
                if (weaponArt != nullptr) {
                    ArtFrame* weaponFrame = frame_ptr(weaponArt, 0, 0);
                    unsigned char* weaponPixels = art_frame_data(weaponArt, 0, 0);
                    if (weaponFrame != nullptr
                        && weaponPixels != nullptr
                        && weaponFrame->width > 0
                        && weaponFrame->height > 0) {
                        int opaqueMinX = weaponFrame->width;
                        int opaqueMinY = weaponFrame->height;
                        int opaqueMaxX = -1;
                        int opaqueMaxY = -1;

                        for (int sy = 0; sy < weaponFrame->height; sy++) {
                            for (int sx = 0; sx < weaponFrame->width; sx++) {
                                if (weaponPixels[sy * weaponFrame->width + sx] != 0) {
                                    opaqueMinX = std::min(opaqueMinX, sx);
                                    opaqueMinY = std::min(opaqueMinY, sy);
                                    opaqueMaxX = std::max(opaqueMaxX, sx);
                                    opaqueMaxY = std::max(opaqueMaxY, sy);
                                }
                            }
                        }

                        if (opaqueMaxX >= opaqueMinX && opaqueMaxY >= opaqueMinY) {
                            const int sourceWidth = opaqueMaxX - opaqueMinX + 1;
                            const int sourceHeight = opaqueMaxY - opaqueMinY + 1;
                            const double maxWidth = width * widthFraction;
                            const double maxHeight = height * 0.56;
                            const double scale = std::min(
                                maxWidth / sourceWidth,
                                maxHeight / sourceHeight);
                            const int drawWidth = std::max(
                                1,
                                static_cast<int>(std::lround(sourceWidth * scale)));
                            const int drawHeight = std::max(
                                1,
                                static_cast<int>(std::lround(sourceHeight * scale)));

                            const int centerX = static_cast<int>(
                                std::lround(width * profile.horizontalCenter));
                            const int left = std::clamp(
                                centerX - drawWidth / 2,
                                -drawWidth + 1,
                                width - 1);
                            const int bottom = static_cast<int>(
                                std::lround(height * bottomFraction));
                            const int top = bottom - drawHeight;

                            // This is the first textured first-person element
                            // migrated off the software framebuffer. The source
                            // remains Fallout's native inventory FRM; SDL uploads
                            // it once and scales/composites it on the GPU.
                            const long long gpuKey =
                                (1LL << 48) | static_cast<unsigned int>(inventoryFid);
                            first_person_gpu_submit_indexed_sprite(
                                gpuKey,
                                weaponPixels,
                                weaponFrame->width,
                                weaponFrame->height,
                                opaqueMinX,
                                opaqueMinY,
                                sourceWidth,
                                sourceHeight,
                                left,
                                top,
                                drawWidth,
                                drawHeight,
                                true);
                        }
                    }

                    art_ptr_unlock(weaponArtKey);
                }
            }
            first_person_heap_check("fp-weapon-after");
        }
    }

    // Pip-Boy date/time strip, top-left, from the Pip-Boy's own art and the
    // native game clock. Everything below it in the top band moves down.
    int topInset = 0;
    {
        const int scale = first_person_hud_layout(width, height).scale;
        const int ids[3] = { 127, 82, 129 }; // PIP.FRM, NUMBERS.FRM, MONTHS.FRM.
        FirstPersonHudArt art[3] = {};
        CacheEntry* keys[3] = {};
        for (int i = 0; i < 3; i++) {
            art[i].data = art_lock(art_id(OBJ_TYPE_INTERFACE, ids[i], 0, 0, 0), &keys[i],
                &art[i].width, &art[i].height);
        }
        if (art[0].data != nullptr) {
            static unsigned char strip[kFirstPersonHudPipDate.width * kFirstPersonHudPipDate.height];
            int month;
            int day;
            int year;
            game_time_date(&month, &day, &year);
            first_person_hud_compose_date(art[0], art[1], art[2], month, day, year, game_time_hour(), strip);
            const FirstPersonHudPlacement date = first_person_hud_date_placement(scale);
            first_person_hud_blit(strip, kFirstPersonHudPipDate.width, kFirstPersonHudPipDate.height,
                date, scale, buffer, width, height);
            topInset = date.y + kFirstPersonHudPipDate.height * scale + 4;
        }
        for (int i = 0; i < 3; i++) {
            if (keys[i] != nullptr) art_ptr_unlock(keys[i]);
        }
    }

    // Original interface-bar pieces: the message monitor, HP/AC counters with
    // the ammo bar, and the AP lights. Native code keeps drawing these into
    // the hidden bar, so copying them shows live native state.
    if (interfaceWindow != -1) {
        const unsigned char* bar = win_get_buf(interfaceWindow);
        const int barWidth = win_width(interfaceWindow);
        const int barHeight = win_height(interfaceWindow);
        if (bar != nullptr && barWidth > 0 && barHeight > 0) {
            const FirstPersonHudLayout hud = first_person_hud_layout(width, height);
            for (const FirstPersonHudPlacement& piece : { hud.monitor, hud.counters, hud.actionPoints }) {
                first_person_hud_blit(bar, barWidth, barHeight, piece, hud.scale, buffer, width, height);
            }
        }
    }

    // First-person mode/combat presentation. Native Fallout remains the
    // authority for combat state, AP, weapon state, range and hit chance; this
    // pass only mirrors that information into the first-person viewport.
    {
        const int mode = first_person_mode();
        const char* modeName = "MOVE";
        switch (mode) {
        case GAME_MOUSE_MODE_ARROW:
            modeName = "INTERACT";
            break;
        case GAME_MOUSE_MODE_CROSSHAIR:
            modeName = "ATTACK";
            break;
        case GAME_MOUSE_MODE_USE_CROSSHAIR:
            modeName = "USE ITEM";
            break;
        case GAME_MOUSE_MODE_USE_FIRST_AID:
            modeName = "FIRST AID";
            break;
        case GAME_MOUSE_MODE_USE_DOCTOR:
            modeName = "DOCTOR";
            break;
        case GAME_MOUSE_MODE_USE_LOCKPICK:
            modeName = "LOCKPICK";
            break;
        case GAME_MOUSE_MODE_USE_STEAL:
            modeName = "STEAL";
            break;
        case GAME_MOUSE_MODE_USE_TRAPS:
            modeName = "TRAPS";
            break;
        case GAME_MOUSE_MODE_USE_SCIENCE:
            modeName = "SCIENCE";
            break;
        case GAME_MOUSE_MODE_USE_REPAIR:
            modeName = "REPAIR";
            break;
        default:
            break;
        }

        char modeLine[160];
        const int action = intface_current_action();
        const char* attackName = action == INTERFACE_ITEM_ACTION_RELOAD ? " / RELOAD"
            : action == INTERFACE_ITEM_ACTION_PRIMARY_AIMING ? " / PRIMARY AIMED"
            : action == INTERFACE_ITEM_ACTION_SECONDARY_AIMING ? " / SECONDARY AIMED"
            : action == INTERFACE_ITEM_ACTION_SECONDARY ? " / SECONDARY"
            : action == INTERFACE_ITEM_ACTION_PRIMARY ? " / PRIMARY" : " / USE ITEM";
        std::snprintf(modeLine, sizeof(modeLine), "MODE: %s%s  View/L4: ACTIONS%s", modeName,
            attackName,
            isInCombat() ? ((intface_is_enabled() && (combat_state & COMBAT_STATE_0x02)) ? "  YOUR TURN" : "  ENEMY TURN") : "");

        char combatLine[256] = { 0 };
        const bool attackPresentation =
            isInCombat() || mode == GAME_MOUSE_MODE_CROSSHAIR;
        if (attackPresentation) {
            int hitMode = 0;
            bool aiming = false;
            const bool haveAttack =
                intface_get_attack(&hitMode, &aiming) == 0;

            const int ap = isInCombat() ? obj_dude->data.critter.combat.ap
                : stat_level(obj_dude, STAT_MAXIMUM_ACTION_POINTS);
            int apCost = -1;
            int ammo = -1;
            int ammoMax = -1;
            Object* weapon = nullptr;

            if (haveAttack) {
                apCost = item_w_mp_cost(obj_dude, hitMode, aiming);
                weapon = item_hit_with(obj_dude, hitMode);
                if (weapon != nullptr) {
                    ammoMax = item_w_max_ammo(weapon);
                    if (ammoMax > 0) {
                        ammo = item_w_curr_ammo(weapon);
                    }
                }
            }

            char targetInfo[128] = "TARGET: --";
            if (firstPersonHoverObject != nullptr
                && FID_TYPE(firstPersonHoverObject->fid) == OBJ_TYPE_CRITTER
                && firstPersonHoverObject != obj_dude) {
                const char* name = critter_name(firstPersonHoverObject);
                if (name == nullptr || *name == '\0') {
                    name = "CRITTER";
                }

                if (haveAttack) {
                    const int badShot = combat_check_bad_shot(
                        obj_dude,
                        firstPersonHoverObject,
                        hitMode,
                        aiming,
                        isInCombat());

                    const char* reason = nullptr;
                    switch (badShot) {
                    case COMBAT_BAD_SHOT_NO_AMMO:
                        reason = "NO AMMO";
                        break;
                    case COMBAT_BAD_SHOT_OUT_OF_RANGE:
                        reason = "OUT OF RANGE";
                        break;
                    case COMBAT_BAD_SHOT_NOT_ENOUGH_AP:
                        reason = "NOT ENOUGH AP";
                        break;
                    case COMBAT_BAD_SHOT_ALREADY_DEAD:
                        reason = "DEAD";
                        break;
                    case COMBAT_BAD_SHOT_AIM_BLOCKED:
                        reason = "AIM BLOCKED";
                        break;
                    case COMBAT_BAD_SHOT_ARM_CRIPPLED:
                        reason = "ARM CRIPPLED";
                        break;
                    case COMBAT_BAD_SHOT_BOTH_ARMS_CRIPPLED:
                        reason = "ARMS CRIPPLED";
                        break;
                    default:
                        break;
                    }

                    if (reason != nullptr) {
                        std::snprintf(
                            targetInfo,
                            sizeof(targetInfo),
                            "TARGET: %s  %s",
                            name,
                            reason);
                    } else {
                        const int accuracy = determine_to_hit(
                            obj_dude,
                            firstPersonHoverObject,
                            HIT_LOCATION_UNCALLED,
                            hitMode);
                        std::snprintf(
                            targetInfo,
                            sizeof(targetInfo),
                            "TARGET: %s  HIT %d%%",
                            name,
                            std::clamp(accuracy, 0, 95));
                    }
                } else {
                    std::snprintf(
                        targetInfo,
                        sizeof(targetInfo),
                        "TARGET: %s",
                        name);
                }
            }

            if (ammoMax > 0) {
                std::snprintf(
                    combatLine,
                    sizeof(combatLine),
                    "AP %d  COST %d  AMMO %d/%d  %s",
                    ap,
                    std::max(0, apCost),
                    std::max(0, ammo),
                    ammoMax,
                    targetInfo);
            } else {
                std::snprintf(
                    combatLine,
                    sizeof(combatLine),
                    "AP %d  COST %d  %s",
                    ap,
                    std::max(0, apCost),
                    targetInfo);
            }
        }

        if (!attackPresentation && firstPersonHoverObject != nullptr) {
            Object* target = firstPersonHoverObject;
            const char* verb = "EXAMINE";
            switch (FID_TYPE(target->fid)) {
            case OBJ_TYPE_ITEM:
                verb = item_get_type(target) == ITEM_TYPE_CONTAINER && !proto_action_can_pickup(target->pid)
                    ? (obj_is_open(target) ? "CLOSE" : "OPEN / LOOT") : "PICK UP";
                break;
            case OBJ_TYPE_CRITTER:
                verb = obj_action_can_talk_to(target) ? "TALK" : "LOOT";
                break;
            case OBJ_TYPE_SCENERY:
                verb = proto_action_can_use(target->pid) ? "USE" : "EXAMINE";
                break;
            }
            if (FID_TYPE(target->fid) == OBJ_TYPE_SCENERY && obj_is_openable(target)) {
                verb = obj_is_open(target) ? "CLOSE" : "OPEN";
            }
            const char* name = object_name(target);
            std::snprintf(combatLine, sizeof(combatLine), "%s: %s%s%s", verb,
                name != nullptr ? name : "Object",
                obj_is_lockable(target) && obj_is_locked(target) ? " [LOCKED]" : "",
                mode == GAME_MOUSE_MODE_MOVE ? "  [M: INTERACT]" : "");
        }
        const int oldFont = text_curr();
        text_font(101);
        const int lineHeight = text_height();
        const int padding = 4;
        const int modeWidth = text_width(modeLine);
        const int combatWidth = combatLine[0] != '\0' ? text_width(combatLine) : 0;
        const int panelWidth = std::min(
            width,
            std::max(modeWidth, combatWidth) + padding * 2);
        const int panelHeight =
            lineHeight * (combatLine[0] != '\0' ? 2 : 1) + padding * 2;

        if (panelWidth > 0 && panelHeight > 0 && topInset + panelHeight <= height) {
            buf_fill(buffer + topInset * width, panelWidth, panelHeight, width, colorTable[0]);
            text_to_buf(
                buffer + (topInset + padding) * width + padding,
                modeLine,
                panelWidth - padding * 2,
                width,
                colorTable[992]);

            if (combatLine[0] != '\0') {
                const int combatColor =
                    firstPersonHoverObject != nullptr
                        && FID_TYPE(firstPersonHoverObject->fid) == OBJ_TYPE_CRITTER
                    ? colorTable[31744]
                    : colorTable[992];
                text_to_buf(
                    buffer + (topInset + padding + lineHeight) * width + padding,
                    combatLine,
                    panelWidth - padding * 2,
                    width,
                    combatColor);
            }
        }

        text_font(oldFont);
    }

    // Reticle prompt: the native hover cursor's action icon for the target,
    // chosen by the same rules (gmouse_3d_primary_action), and its name.
    const int promptMode = first_person_mode();
    if (firstPersonHoverObject != nullptr && firstPersonHoverObject != obj_dude
        && (promptMode == GAME_MOUSE_MODE_ARROW || promptMode == GAME_MOUSE_MODE_MOVE)) {
        const int scale = first_person_hud_layout(width, height).scale;
        const int iconX = width / 2 + 12 * scale;
        int iconBottom = height / 2 + 6 * scale;
        CacheEntry* iconEntry = nullptr;
        const int iconFid = gmouse_3d_action_icon_fid(gmouse_3d_primary_action(firstPersonHoverObject));
        Art* icon = iconFid != -1 ? art_ptr_lock(iconFid, &iconEntry) : nullptr;
        if (icon != nullptr) {
            const unsigned char* iconData = art_frame_data(icon, 0, 0);
            const int iconWidth = art_frame_width(icon, 0, 0);
            const int iconHeight = art_frame_length(icon, 0, 0);
            if (iconData != nullptr && iconWidth > 0 && iconHeight > 0) {
                first_person_hud_blit_sprite(iconData, iconWidth, iconHeight, iconX,
                    height / 2 - iconHeight * scale / 2, scale, buffer, width, height);
                iconBottom = height / 2 - iconHeight * scale / 2 + iconHeight * scale + 2 * scale;
            }
            art_ptr_unlock(iconEntry);
        }
        if (const char* name = object_name(firstPersonHoverObject)) {
            const int oldFont = text_curr();
            text_font(101);
            const int nameWidth = std::min(text_width(name), width / 3);
            const int lineHeight = text_height();
            if (iconBottom + lineHeight + 4 <= height && iconX + nameWidth + 4 <= width) {
                buf_fill(buffer + iconBottom * width + iconX, nameWidth + 4, lineHeight + 4, width, colorTable[0]);
                text_to_buf(buffer + (iconBottom + 2) * width + iconX + 2, name, nameWidth, width, colorTable[992]);
            }
            text_font(oldFont);
        }
    }

    // Controller status is diagnostic: show it only while recording input.
    static const bool showControllerStatus = std::getenv("FALLOUT_FP_INPUT_LOG") != nullptr;
    if (showControllerStatus) {
        const int oldFont = text_curr();
        text_font(101);
        const int diagnosticY = std::min(height - text_height() - 4, topInset + text_height() * 4 + 24);
        buf_fill(buffer + diagnosticY * width, width, text_height() + 4, width, colorTable[0]);
        text_to_buf(buffer + (diagnosticY + 2) * width + 4, gamepad_diagnostic(),
            width - 8, width, colorTable[992]);
        text_font(oldFont);
    }
    if (gamepad_controller() != nullptr) {
        const int oldFont = text_curr();
        text_font(101);
        const int lineHeight = text_height();
        // Top-right: the bottom-left corner belongs to the message monitor.
        const int hintY = 4;
        const int hintWidth = std::min(width, 570);
        const int hintX = width - hintWidth;
        buf_fill(buffer + hintY * width + hintX, hintWidth, lineHeight * 2 + 4, width, colorTable[0]);
        text_to_buf(buffer + (hintY + 2) * width + hintX + 4,
            "A: interact  RT: action  LT: mode  X: inventory  Y: Pip-Boy",
            hintWidth - 8, width, colorTable[992]);
        text_to_buf(buffer + (hintY + lineHeight + 2) * width + hintX + 4,
            gamepad_has_paddles() ? "L4/View: actions  R4: reload  L5: end turn  R5/R3: view"
                : "View: actions/reload/end turn  R3: view  LB: hand  RB: attack type",
            hintWidth - 8, width, colorTable[992]);
        text_font(oldFont);
    }

    if (gFirstPersonNoticeUntil != 0) {
        const int oldFont = text_curr();
        text_font(101);
        const int messageY = std::min(height - text_height() - 4, topInset + text_height() * 2 + 16);
        buf_fill(buffer + messageY * width, width, text_height() + 4, width, colorTable[0]);
        text_to_buf(buffer + (messageY + 2) * width + 4, gFirstPersonNotice,
            width - 8, width, colorTable[992]);
        text_font(oldFont);
    }

    // First-person world interaction is camera-centered. Do not draw a second
    // free mouse pointer over the scene; native modal windows still own the
    // real pointer when they are shown above the first-person overlay.
    // The center reticle is now composed in the SDL GPU pass after the
    // software framebuffer upload. Keep the world renderer focused on scene
    // generation while we migrate more first-person drawing to the GPU.

    if (viewWindow != display_win) {
        win_draw(viewWindow);
    }
}

} // namespace fallout

namespace fallout {

// Diagnostic map sweep (FALLOUT_FP_MAP_SWEEP, see main.cc). Renders the loaded
// map from spread-out standing spots on every used elevation, four headings
// each, and writes one TSV row per frame. Native state is only read, apart
// from moving the player between spots; scripts are not run.
int first_person_sweep_map(std::FILE* out, const char* mapName, void (*progress)())
{
    constexpr int kTileCount = 200 * 200;
    constexpr int kSpotsPerElevation = 12;
    if (!gFirstPersonEnabled) first_person_toggle();
    const int startElevation = map_elevation;
    const int startTile = obj_dude->tile;
    int frames = 0;
    for (int elevation = 0; elevation < ELEVATION_COUNT; elevation++) {
        std::vector<int> structure;
        std::vector<unsigned char> avoid(kTileCount, 0);
        for (Object* object = obj_find_first_at(elevation); object != nullptr; object = obj_find_next_at()) {
            if (object->tile < 0 || object->tile >= kTileCount || object == obj_dude) continue;
            const int type = FID_TYPE(object->fid);
            if (type == OBJ_TYPE_WALL || type == OBJ_TYPE_SCENERY) structure.push_back(object->tile);
            // Exit grids would queue a map transition when stepped on.
            if (type == OBJ_TYPE_MISC) avoid[object->tile] = 1;
        }
        if (structure.empty()) continue;

        // Ceiling alignment: how many walls sit under a roof square at each
        // square offset. Roofs share their floor's square, so (0,0) should win.
        {
            const int noRoofFid = art_id(OBJ_TYPE_TILE, 1, 0, 0, 0);
            int roofSquares = 0;
            for (int i = 0; i < SQUARE_GRID_SIZE; i++) {
                if (art_id(OBJ_TYPE_TILE, (square[elevation]->field_0[i] >> 16) & 0xFFF, 0, 0, 0) != noRoofFid) roofSquares++;
            }
            int counts[7][7] = {};
            int walls = 0;
            for (Object* object = obj_find_first_at(elevation); object != nullptr; object = obj_find_next_at()) {
                if (FID_TYPE(object->fid) != OBJ_TYPE_WALL || object->tile < 0) continue;
                int x;
                int y;
                tile_coord(object->tile, &x, &y, elevation);
                const int squareTile = square_num(x + 16, y + 8, elevation);
                if (squareTile < 0 || squareTile >= SQUARE_GRID_SIZE) continue;
                walls++;
                for (int oy = -3; oy <= 3; oy++) {
                    for (int ox = -3; ox <= 3; ox++) {
                        const int sx = squareTile % 100 + ox;
                        const int sy = squareTile / 100 + oy;
                        if (sx < 0 || sx >= 100 || sy < 0 || sy >= 100) continue;
                        if (art_id(OBJ_TYPE_TILE, (square[elevation]->field_0[sy * 100 + sx] >> 16) & 0xFFF, 0, 0, 0) != noRoofFid) {
                            counts[oy + 3][ox + 3]++;
                        }
                    }
                }
            }
            int bestX = 0;
            int bestY = 0;
            for (int oy = -3; oy <= 3; oy++) {
                for (int ox = -3; ox <= 3; ox++) {
                    if (counts[oy + 3][ox + 3] > counts[bestY + 3][bestX + 3]) {
                        bestX = ox;
                        bestY = oy;
                    }
                }
            }
            std::fprintf(out, "ROOF\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", mapName, elevation, roofSquares,
                walls, counts[3][3], bestX, bestY, counts[bestY + 3][bestX + 3]);
            int enclosed = 0;
            for (unsigned char covered : first_person_enclosed_ceiling(elevation)) enclosed += covered;
            int floored = 0;
            for (int i = 0; i < SQUARE_GRID_SIZE; i++) {
                if ((square[elevation]->field_0[i] & 0xFFF) != 1) floored++;
            }
            std::fprintf(out, "CEILING\t%s\t%d\t%d\t%d\n", mapName, elevation, enclosed, floored);
        }

        // Sprite anchoring: native art offsets and the empty rows below each
        // critter's and item's current frame, in isometric pixels.
        for (int spriteType : { OBJ_TYPE_CRITTER, OBJ_TYPE_ITEM }) {
            int sprites = 0;
            int sumX = 0;
            int sumY = 0;
            int maxY = 0;
            int sumMargin = 0;
            int maxMargin = 0;
            for (Object* object = obj_find_first_at(elevation); object != nullptr; object = obj_find_next_at()) {
                if (FID_TYPE(object->fid) != spriteType || object->tile < 0 || object == obj_dude) continue;
                CacheEntry* entry = nullptr;
                Art* art = art_ptr_lock(object->fid, &entry);
                if (art == nullptr) continue;
                const int direction = ((object->rotation % ROTATION_COUNT) + ROTATION_COUNT) % ROTATION_COUNT;
                const int frameNumber = std::clamp(object->frame, 0, std::max(0, art_frame_max_frame(art) - 1));
                ArtFrame* frame = frame_ptr(art, frameNumber, direction);
                const unsigned char* pixels = art_frame_data(art, frameNumber, direction);
                if (frame != nullptr && pixels != nullptr && frame->width > 0 && frame->height > 0) {
                    int lowest = -1;
                    for (int y = frame->height - 1; y >= 0 && lowest < 0; y--) {
                        for (int x = 0; x < frame->width; x++) {
                            if (pixels[y * frame->width + x] != 0) {
                                lowest = y;
                                break;
                            }
                        }
                    }
                    const int margin = lowest < 0 ? 0 : frame->height - 1 - lowest;
                    sprites++;
                    sumX += std::abs(art->xOffsets[direction]);
                    sumY += std::abs(art->yOffsets[direction]);
                    maxY = std::max(maxY, std::abs(static_cast<int>(art->yOffsets[direction])));
                    sumMargin += margin;
                    maxMargin = std::max(maxMargin, margin);
                }
                art_ptr_unlock(entry);
            }
            if (sprites > 0) {
                std::fprintf(out, "SPRITES\t%s\t%d\t%s\t%d\t%.1f\t%.1f\t%d\t%.1f\t%d\n", mapName, elevation,
                    spriteType == OBJ_TYPE_CRITTER ? "critter" : "item", sprites,
                    static_cast<double>(sumX) / sprites, static_cast<double>(sumY) / sprites, maxY,
                    static_cast<double>(sumMargin) / sprites, maxMargin);
            }
        }

        // Unblocked hexes on a coarse grid, near walls or scenery (within
        // about six hexes, marked once rather than measured pairwise).
        std::vector<unsigned char> nearStructure(kTileCount, 0);
        for (int wall : structure) {
            const int wallRow = wall / 200;
            const int wallColumn = wall % 200;
            for (int row = std::max(0, wallRow - 6); row <= std::min(199, wallRow + 6); row++) {
                for (int column = std::max(0, wallColumn - 6); column <= std::min(199, wallColumn + 6); column++) {
                    nearStructure[row * 200 + column] = 1;
                }
            }
        }
        std::vector<int> candidates;
        for (int row = 3; row < 200; row += 7) {
            for (int column = 3; column < 200; column += 7) {
                const int tile = row * 200 + column;
                if (!nearStructure[tile] || avoid[tile]
                    || obj_blocking_at(nullptr, tile, elevation) != nullptr) {
                    continue;
                }
                candidates.push_back(tile);
            }
        }
        std::vector<int> spots;
        if (elevation == startElevation) spots.push_back(startTile);
        const size_t want = std::min<size_t>(kSpotsPerElevation, candidates.size());
        for (size_t i = 0; i < want; i++) spots.push_back(candidates[i * candidates.size() / want]);

        for (int spot : spots) {
            if (map_elevation != elevation) map_set_elevation(elevation);
            obj_move_to_tile(obj_dude, spot, elevation, nullptr);
            for (int heading = 0; heading < 24; heading += 6) {
                gFirstPersonHeading = heading;
                gFirstPersonPitchDegrees = 0.0;
                gFirstPersonCameraRevision++;
                const Uint64 start = SDL_GetPerformanceCounter();
                first_person_render_now();
                const double ms = (SDL_GetPerformanceCounter() - start) * 1000.0
                    / static_cast<double>(SDL_GetPerformanceFrequency());
                size_t covered = 0;
                std::vector<int> ids;
                for (const FirstPersonPick& pick : gFirstPersonPicks) {
                    if (pick.object == nullptr) continue;
                    covered++;
                    if (ids.empty() || ids.back() != pick.id) ids.push_back(pick.id);
                }
                std::sort(ids.begin(), ids.end());
                ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
                const double coverage = gFirstPersonPicks.empty() ? -1.0
                    : static_cast<double>(covered) / gFirstPersonPicks.size();
                std::fprintf(out, "FRAME\t%s\t%d\t%d\t%d\t%.2f\t%.3f\t%zu\t%s\n", mapName, elevation,
                    spot, heading, ms, coverage, ids.size(), gFirstPersonLastFrameGpu ? "gpu" : "software");
                frames++;
                progress();
            }
            std::fflush(out);
        }
    }
    if (map_elevation != startElevation) map_set_elevation(startElevation);
    obj_move_to_tile(obj_dude, startTile, startElevation, nullptr);
    return frames;
}

} // namespace fallout
