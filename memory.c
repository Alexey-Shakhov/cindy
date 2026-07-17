#include <stdlib.h>

#define MEM_ALIGNMENT 8

#define KBS(n) (1024*(n))
#define MBS(n) (1024*1024*(n))
#define GBS(n) (1024*1024*1024*(n))

typedef struct Arena {
    uint8_t* buffer;
    size_t capacity;
    size_t offset;
} Arena;

typedef struct Marker {
    Arena* arena;
    size_t offset;
} Marker;

struct {
    Arena total;
    Arena scratch;
} memory;

void* arena_alloc(Arena* arena, size_t amount) {
    if (amount == 0) {
        fatal("Zero-sized allocation attempt.");
    }
    uintptr_t current = (uintptr_t) arena->buffer + arena->offset;
    uintptr_t aligned = (current + (MEM_ALIGNMENT - 1)) & ~(MEM_ALIGNMENT - 1);
    size_t new_offset = (aligned - (uintptr_t) arena->buffer) + amount;
    if (new_offset > arena->capacity) {
        fatal("Arena out of memory.");
    }
    arena->offset = new_offset;
    return (void*) aligned;
}

Arena arena_init(Arena* arena, size_t capacity) {
    uint8_t* buf;
    if (arena) {
        buf = arena_alloc(arena, capacity);
    } else {
        buf = malloc(capacity);
    }
    if (!buf) {
        fatal("Failed to allocate arena memory.");
    }
    return (Arena) {
        .buffer = buf,
        .capacity = capacity,
        .offset = 0
    };
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

void* marker_alloc(const Marker marker, size_t amount) {
    return arena_alloc(marker.arena, amount);
}

void marker_reset(const Marker marker) {
    marker.arena->offset = marker.offset;
}

void memory_init(size_t total_size, size_t scratch_size) {
    memory.total = arena_init(NULL, total_size);
    memory.scratch = arena_init(&memory.total, scratch_size);
}

void memory_shutdown() {
    arena_free(&memory.total);
}
