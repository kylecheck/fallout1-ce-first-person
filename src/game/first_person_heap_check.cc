#include "game/first_person_heap_check.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "game/anim.h"
#include "game/art.h"
#include "game/gamepad.h"
#include "game/gmouse.h"

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

static void append_owner(Cache* cache, const char* field, int handleIndex,
    const char* (*artName)(int key), char* report, size_t reportSize)
{
    const size_t used = std::strlen(report);
    if (used >= reportSize) return;
    char* out = report + used;
    const size_t left = reportSize - used;
    if (handleIndex < 0) {
        std::snprintf(out, left, " %s=none", field);
        return;
    }
    for (int index = 0; index < cache->entriesLength; index++) {
        const CacheEntry* entry = cache->entries[index];
        if (entry == nullptr || entry->heapHandleIndex != handleIndex) continue;
        const char* name = artName != nullptr ? artName(entry->key) : nullptr;
        std::snprintf(out, left, " %s=handle:%d,key:0x%08x,type:%d,id:%d,size:%d,refs:%u,art:%s",
            field, handleIndex, static_cast<unsigned int>(entry->key),
            (entry->key & 0xF000000) >> 24, entry->key & 0xFFF, entry->size,
            entry->referenceCount, name != nullptr ? name : "?");
        return;
    }
    std::snprintf(out, left, " %s=handle:%d,no-cache-entry", field, handleIndex);
}

bool first_person_heap_check_cache(Cache* cache, const char* label,
    unsigned long long frame, const char* (*artName)(int key),
    char* report, size_t reportSize)
{
    if (first_person_heap_check_heap(&cache->heap, label, frame, report, reportSize)) return true;
    int handleIndex;
    int previousHandleIndex;
    heap_validate_last_failure_handles(&handleIndex, &previousHandleIndex);
    append_owner(cache, "owner", handleIndex, artName, report, reportSize);
    append_owner(cache, "previous_owner", previousHandleIndex, artName, report, reportSize);
    return false;
}

static const char* first_person_heap_check_art_name(int key)
{
    return art_get_name(key);
}

void first_person_heap_check(const char* label)
{
    if (!first_person_heap_check_enabled()) return;
    static bool announced = false;
    if (!announced) {
        announced = true;
        gamepad_log("HEAP_CHECK enabled heap_size=%d", art_cache.heap.size);
    }
    char report[1024];
    if (first_person_heap_check_cache(&art_cache, label, gHeapCheckFrame,
            first_person_heap_check_art_name, report, sizeof(report))) {
        return;
    }
    gamepad_log("%s", report);
    std::fprintf(stderr, "%s\n", report);
    std::fflush(stderr);
    std::abort();
}

void first_person_heap_check_bk(void (*process)())
{
    if (!first_person_heap_check_enabled()) return;
    if (process == gmouse_bk_process) {
        first_person_heap_check("bk-gmouse");
    } else if (process == object_animate) {
        first_person_heap_check("bk-object-animate");
    } else if (process == dude_fidget) {
        first_person_heap_check("bk-dude-fidget");
    } else {
        // Static processes (sound, colour cycling, text, mouse animation):
        // the offset from gmouse_bk_process identifies them with nm.
        char label[64];
        std::snprintf(label, sizeof(label), "bk-gmouse%+lld",
            static_cast<long long>(reinterpret_cast<std::intptr_t>(process)
                - reinterpret_cast<std::intptr_t>(gmouse_bk_process)));
        first_person_heap_check(label);
    }
}

} // namespace fallout
