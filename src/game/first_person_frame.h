#ifndef FALLOUT_GAME_FIRST_PERSON_FRAME_H_
#define FALLOUT_GAME_FIRST_PERSON_FRAME_H_
namespace fallout {
class FirstPersonFrameRequest {
public:
    void request() { dirty_ = true; }
    bool take(bool enabled, bool suspended) {
        if (!dirty_ || !enabled || suspended) return false;
        dirty_ = false;
        return true;
    }
private:
    bool dirty_ = true;
};
}
#endif
