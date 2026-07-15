#include <stdlib.h>
#include "utils.c"

#define MEM_ALIGNMENT 8

typedef struct Arena {
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
} Arena;

typedef struct Marker {
    Arena* arena;
    size_t offset;
} Marker;

Arena arena_init(size_t capacity) {
    uint8_t* buf = malloc(capacity);
    if (!buf) {
        fatal("Failed to allocate arena memory.");
    }
    return (Arena) {
        .buffer = buf,
        .capacity = capacity,
        .offset = 0
    };
}

void* arena_alloc(Arena* arena, size_t amount) {
    uintptr_t current = (uintptr_t) arena->buffer + arena->offset;
    uintptr_t aligned = (current + (MEM_ALIGNMENT - 1)) & ~(MEM_ALIGNMENT - 1);
    size_t new_offset = (aligned - (uintptr_t) arena->buffer) + amount;
    if (new_offset > arena->capacity) {
        fatal("Arena out of memory.");
    }
    arena->offset = new_offset;
    return (void*) aligned;
}

void arena_reset(Arena* arena) {
    arena->offset = 0;
}

void arena_free(Arena* arena) {
    free(arena->buffer);
}

Marker marker_new(Arena* arena) {
    return (Marker) {
        .arena = arena,
        .offset = arena->offset
    };
}

void* marker_alloc(Marker* marker, size_t amount) {
    return arena_alloc(marker->arena, amount);
}

void marker_reset(Marker* marker) {
    marker->arena->offset = marker->offset;
}
