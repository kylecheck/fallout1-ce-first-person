#include "game/first_person_heap_check.h"

#include <cstdio>
#include <cstdlib>

#include "game/art.h"
#include "game/cache.h"
#include "game/gamepad.h"

namespace fallout {

static unsigned long long gHeapCheckFrame = 0;

bool first_person_heap_check_enabled()
{
    static const bool enabled = [] {
        const char* value = std::getenv("FALLOUT_FP_HEAP_CHECK");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

void first_person_heap_check_frame()
{
    if (first_person_heap_check_enabled()) gHeapCheckFrame++;
}

bool first_person_heap_check_heap(Heap* heap, const char* label,
    unsigned long long frame, char* report, size_t reportSize)
{
    if (heap_validate(heap)) return true;
    std::snprintf(report, reportSize,
        "HEAP_CHECK_FAILED label=%s frame=%llu reason=%s heap_size=%d"
        " free=%d/%d moveable=%d/%d locked=%d/%d system=%d/%d",
        label, frame, heap_validate_last_failure(), heap->size,
        heap->freeBlocks, heap->freeSize, heap->moveableBlocks, heap->moveableSize,
        heap->lockedBlocks, heap->lockedSize, heap->systemBlocks, heap->systemSize);
    return false;
}

void first_person_heap_check(const char* label)
{
    if (!first_person_heap_check_enabled()) return;
    static bool announced = false;
    if (!announced) {
        announced = true;
        gamepad_log("HEAP_CHECK enabled heap_size=%d", art_cache.heap.size);
    }
    char report[512];
    if (first_person_heap_check_heap(&art_cache.heap, label, gHeapCheckFrame, report, sizeof(report))) return;
    gamepad_log("%s", report);
    std::fprintf(stderr, "%s\n", report);
    std::fflush(stderr);
    std::abort();
}

} // namespace fallout
