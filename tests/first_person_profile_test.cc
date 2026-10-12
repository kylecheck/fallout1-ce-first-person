#include "game/first_person_profile.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

using namespace fallout;

int main()
{
    FirstPersonProfile profile;
    char line[512];
    std::uint64_t t = 1000;
    // 1 tick = 1 ms. Frames: floor 2 ms, walls 3 ms (5 ms on the last), gpu read 1 ms.
    for (int frame = 0; frame < FirstPersonProfile::kFrames; frame++) {
        profile.begin(t);
        t += 2;
        profile.mark(FP_PROFILE_FLOOR, t);
        t += frame == FirstPersonProfile::kFrames - 1 ? 5 : 3;
        profile.mark(FP_PROFILE_WALLS, t);
        t += 1;
        profile.mark(FP_PROFILE_GPU_READ, t);
        profile.mark(FP_PROFILE_STAGE_COUNT, t); // Out of range: ignored.
        t += 4; // Untimed gap between frames.
        const bool due = profile.end(t - 4, 1000, "VAULT13.MAP", 1, frame % 4 != 0, line, sizeof(line));
        assert(due == (frame == FirstPersonProfile::kFrames - 1));
    }
    const std::string text = line;
    assert(text.rfind("PROFILE\tVAULT13.MAP\t1\tgpu\t300\t", 0) == 0);
    assert(text.find("\ttotal=6.01/8.00") != std::string::npos);
    assert(text.find("\tfloor=2.00/2.00") != std::string::npos);
    assert(text.find("\twalls=3.01/5.00") != std::string::npos);
    assert(text.find("\tgpu_read=1.00/1.00") != std::string::npos);
    assert(text.find("\tobjects=0.00/0.00") != std::string::npos);
    // 300 frames over 3000 ms of wall time (10 ms per frame including gaps).
    assert(text.find("\t100.0\t") != std::string::npos || text.find("\t100.1\t") != std::string::npos);

    // The next window starts clean.
    profile.begin(t);
    t += 1;
    profile.mark(FP_PROFILE_OVERLAY, t);
    assert(!profile.end(t, 1000, "X", 0, false, line, sizeof(line)));
    // A short buffer is never overrun.
    for (int frame = 1; frame < FirstPersonProfile::kFrames; frame++) {
        profile.begin(t);
        profile.end(t, 1000, "X", 0, false, line, 32);
    }
    char small[40];
    std::memset(small, 'Z', sizeof(small));
    profile.begin(t);
    for (int frame = 0; frame < FirstPersonProfile::kFrames; frame++) {
        profile.begin(t);
        profile.end(t, 1000, "X", 0, false, small, 32);
    }
    assert(small[31] == '\0' && small[32] == 'Z');
    std::cout << "PASS first-person stage profile: averages, worst cases, rate and bounds\n";
}
