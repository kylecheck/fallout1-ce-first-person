#ifndef FALLOUT_GAME_FIRST_PERSON_PROFILE_H_
#define FALLOUT_GAME_FIRST_PERSON_PROFILE_H_
#include <algorithm>
#include <cstdint>
#include <cstdio>
namespace fallout {
// Opt-in per-stage timing of first-person frames (FALLOUT_FP_PROFILE=<file>).
// Each frame is split at stage marks; every kFrames frames one summary line
// gives the average and worst time per stage in milliseconds, how many
// frames were rendered per second of wall time, and where.
enum FirstPersonProfileStage {
    FP_PROFILE_SETUP,
    FP_PROFILE_FLOOR,
    FP_PROFILE_TARGET,
    FP_PROFILE_TOPOLOGY,
    FP_PROFILE_WALLS,
    FP_PROFILE_DOORS,
    FP_PROFILE_OBJECTS,
    FP_PROFILE_GPU_READ,
    FP_PROFILE_OVERLAY,
    FP_PROFILE_STAGE_COUNT,
};

inline const char* first_person_profile_stage_name(int stage)
{
    static const char* names[FP_PROFILE_STAGE_COUNT] = { "setup", "floor", "target", "topology",
        "walls", "doors", "objects", "gpu_read", "overlay" };
    return stage >= 0 && stage < FP_PROFILE_STAGE_COUNT ? names[stage] : "?";
}

class FirstPersonProfile {
public:
    static constexpr int kFrames = 300;

    // ticks/frequency as SDL_GetPerformanceCounter/-Frequency.
    void begin(std::uint64_t ticks)
    {
        frameStart_ = last_ = ticks;
        if (frames_ == 0) windowStart_ = ticks;
        std::fill(current_, current_ + FP_PROFILE_STAGE_COUNT, 0);
    }
    // Time since the previous mark (or begin) belongs to stage.
    void mark(int stage, std::uint64_t ticks)
    {
        if (stage >= 0 && stage < FP_PROFILE_STAGE_COUNT) current_[stage] += ticks - last_;
        last_ = ticks;
    }
    // Returns true and fills line when a summary is due.
    bool end(std::uint64_t ticks, std::uint64_t frequency, const char* map, int elevation, bool gpu,
        char* line, size_t lineSize)
    {
        const std::uint64_t total = ticks - frameStart_;
        totalSum_ += total;
        totalMax_ = std::max(totalMax_, total);
        for (int i = 0; i < FP_PROFILE_STAGE_COUNT; i++) {
            sum_[i] += current_[i];
            max_[i] = std::max(max_[i], current_[i]);
        }
        gpuFrames_ += gpu ? 1 : 0;
        if (++frames_ < kFrames) return false;

        const double ms = 1000.0 / static_cast<double>(frequency);
        const double seconds = (ticks - windowStart_) / static_cast<double>(frequency);
        int used = std::snprintf(line, lineSize, "PROFILE\t%s\t%d\t%s\t%d\t%.1f\ttotal=%.2f/%.2f", map, elevation,
            gpuFrames_ * 2 >= frames_ ? "gpu" : "software", frames_, seconds > 0.0 ? frames_ / seconds : 0.0,
            totalSum_ * ms / frames_, totalMax_ * ms);
        for (int i = 0; i < FP_PROFILE_STAGE_COUNT && used >= 0 && static_cast<size_t>(used) < lineSize; i++) {
            used += std::snprintf(line + used, lineSize - used, "\t%s=%.2f/%.2f", first_person_profile_stage_name(i),
                sum_[i] * ms / frames_, max_[i] * ms);
        }
        frames_ = 0;
        gpuFrames_ = 0;
        totalSum_ = totalMax_ = 0;
        std::fill(sum_, sum_ + FP_PROFILE_STAGE_COUNT, 0);
        std::fill(max_, max_ + FP_PROFILE_STAGE_COUNT, 0);
        return true;
    }

private:
    std::uint64_t frameStart_ = 0;
    std::uint64_t last_ = 0;
    std::uint64_t windowStart_ = 0;
    std::uint64_t current_[FP_PROFILE_STAGE_COUNT] = {};
    std::uint64_t sum_[FP_PROFILE_STAGE_COUNT] = {};
    std::uint64_t max_[FP_PROFILE_STAGE_COUNT] = {};
    std::uint64_t totalSum_ = 0;
    std::uint64_t totalMax_ = 0;
    int frames_ = 0;
    int gpuFrames_ = 0;
};
}
#endif
