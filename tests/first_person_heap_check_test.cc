// Compile with section GC: the checkpoint wrapper's art-cache and input-log
// references are discarded, leaving the real native heap and report formatter.
#include "game/heap.cc"
#include "game/first_person_heap_check.cc"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace fallout {
void* mem_malloc(size_t size) { return std::malloc(size); }
void* mem_realloc(void* ptr, size_t size) { return std::realloc(ptr, size); }
void mem_free(void* ptr) { std::free(ptr); }
int debug_printf(const char*, ...) { return 0; }
}

using namespace fallout;

static HeapBlockHeader* header(unsigned char* buffer)
{
    return reinterpret_cast<HeapBlockHeader*>(buffer - HEAP_BLOCK_HEADER_SIZE);
}

static bool check(Heap* heap, char* report, size_t size)
{
    report[0] = '\0';
    return first_person_heap_check_heap(heap, "test-label", 42, report, size);
}

int main()
{
    unsetenv("FALLOUT_FP_HEAP_CHECK");
    assert(!first_person_heap_check_enabled()); // Off by default.

    Heap heap;
    assert(heap_init(&heap, 64 * 1024));
    int handles[3];
    unsigned char* buffers[3];
    for (int i = 0; i < 3; i++) {
        assert(heap_allocate(&heap, &handles[i], 1000 + i * 100, 0));
        assert(heap_lock(&heap, handles[i], &buffers[i]));
    }
    assert(heap_unlock(&heap, handles[2])); // Mix moveable and locked blocks.

    char report[512];
    assert(check(&heap, report, sizeof(report)));
    assert(report[0] == '\0');
    assert(heap_validate_last_failure()[0] == '\0');

    // One-byte overrun past the end of a block's data hits its footer guard.
    HeapBlockHeader* middle = header(buffers[1]);
    unsigned char saved = buffers[1][middle->size];
    buffers[1][middle->size] ^= 0xFF;
    assert(!check(&heap, report, sizeof(report)));
    assert(std::strstr(report, "HEAP_CHECK_FAILED label=test-label frame=42") == report);
    assert(std::strstr(report, "reason=Bad guard end detected block=1 offset=") != nullptr);
    assert(std::strstr(report, "locked=2/") != nullptr);
    buffers[1][middle->size] = saved;
    assert(check(&heap, report, sizeof(report)));

    // An underrun into the next header's guard.
    HeapBlockHeader* last = header(buffers[2]);
    const int guard = last->guard;
    last->guard = 0;
    assert(!check(&heap, report, sizeof(report)));
    assert(std::strstr(report, "reason=Bad guard begin detected block=2") != nullptr);
    last->guard = guard;

    // A garbage size must be reported rather than followed outside the heap.
    for (int bad : { -1, 0x7FFFFFF0, heap.size }) {
        const int size = middle->size;
        middle->size = bad;
        assert(!check(&heap, report, sizeof(report)));
        assert(std::strstr(report, "reason=Bad block size detected block=1") != nullptr);
        middle->size = size;
    }

    // Counter drift, as from unbalanced lock/unlock bookkeeping. An extra
    // block count makes the walk leave the heap; a moved one is miscounted.
    heap.lockedBlocks++;
    assert(!check(&heap, report, sizeof(report)));
    assert(std::strstr(report, "reason=Ran off end of heap block=") != nullptr);
    heap.lockedBlocks--;
    heap.lockedBlocks--;
    heap.moveableBlocks++;
    assert(!check(&heap, report, sizeof(report)));
    assert(std::strstr(report, "reason=Invalid number of moveable blocks") != nullptr);
    heap.moveableBlocks--;
    heap.lockedBlocks++;

    assert(check(&heap, report, sizeof(report)));
    assert(heap_validate_last_failure()[0] == '\0');
    for (int i = 0; i < 2; i++) assert(heap_unlock(&heap, handles[i]));
    for (int& handle : handles) assert(heap_deallocate(&heap, &handle));
    assert(check(&heap, report, sizeof(report)));
    heap_exit(&heap);
    std::cout << "PASS heap check reports overruns, bad sizes and counter drift\n";
}
