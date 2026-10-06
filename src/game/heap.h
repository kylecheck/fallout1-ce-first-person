#ifndef FALLOUT_GAME_HEAP_H_
#define FALLOUT_GAME_HEAP_H_

#include <stddef.h>

namespace fallout {

typedef struct HeapHandle {
    unsigned int state;
    unsigned char* data;
} HeapHandle;

typedef struct Heap {
    int size;
    int freeBlocks;
    int moveableBlocks;
    int lockedBlocks;
    int systemBlocks;
    int handlesLength;
    int freeSize;
    int moveableSize;
    int lockedSize;
    int systemSize;
    HeapHandle* handles;
    unsigned char* data;
} Heap;

bool heap_init(Heap* heap, int a2);
bool heap_exit(Heap* heap);
bool heap_allocate(Heap* heap, int* handleIndexPtr, int size, int a3);
bool heap_deallocate(Heap* heap, int* handleIndexPtr);
bool heap_lock(Heap* heap, int handleIndex, unsigned char** bufferPtr);
bool heap_unlock(Heap* heap, int handleIndex);
bool heap_stats(Heap* heap, char* dest, size_t size);
bool heap_validate(Heap* heap);
// Reason for the most recent heap_validate failure; empty after success.
const char* heap_validate_last_failure();
// Heap handles of the failing block and the block before it, or -1.
void heap_validate_last_failure_handles(int* handleIndex, int* previousHandleIndex);

} // namespace fallout

#endif /* FALLOUT_GAME_HEAP_H_ */
