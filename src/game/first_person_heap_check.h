#ifndef FALLOUT_GAME_FIRST_PERSON_HEAP_CHECK_H_
#define FALLOUT_GAME_FIRST_PERSON_HEAP_CHECK_H_

#include <stddef.h>

#include "game/cache.h"
#include "game/heap.h"

namespace fallout {

// Opt-in art-cache heap validation: FALLOUT_FP_HEAP_CHECK=1. Each labelled
// checkpoint validates every block; the first failure is written to the input
// log and stderr, then the game aborts at that checkpoint.
bool first_person_heap_check_enabled();
void first_person_heap_check(const char* label);
// Checkpoint after one native background process, labelled by function.
void first_person_heap_check_bk(void (*process)());
// Counts first-person frames so a failure names the frame it followed.
void first_person_heap_check_frame();
// Validates one heap. On failure, writes a HEAP_CHECK_FAILED line to report.
bool first_person_heap_check_heap(Heap* heap, const char* label,
    unsigned long long frame, char* report, size_t reportSize);
// As above for a cache, also naming the cache entries that own the damaged
// block and the block before it. artName may be null.
bool first_person_heap_check_cache(Cache* cache, const char* label,
    unsigned long long frame, const char* (*artName)(int key),
    char* report, size_t reportSize);

} // namespace fallout

#endif /* FALLOUT_GAME_FIRST_PERSON_HEAP_CHECK_H_ */
