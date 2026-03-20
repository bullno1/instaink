#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdint.h>

#define STATIC_ARENA(name, size) \
    static char name##_buf[(size)]; \
    static arena_t name = { \
        .start   = name##_buf, \
        .current = name##_buf, \
        .end     = name##_buf + (size) \
    }

typedef struct {
	char* start;
	char* current;
	char* end;
} arena_t;

static inline void
arena_init(arena_t* arena, void* memory, size_t size) {
	arena->start   = (char*)memory;
	arena->current = (char*)memory;
	arena->end     = (char*)memory + size;
}

static inline void*
arena_memalign(arena_t* arena, size_t size, size_t alignment) {
	// alignment must be a power of two
	char* aligned = (char*)(((uintptr_t)arena->current + (alignment - 1)) & ~(alignment - 1));

	if (aligned + size > arena->end) {
		return NULL;
	}

	arena->current = aligned + size;
	return aligned;
}

static inline void
arena_reset(arena_t* arena) {
	arena->current = arena->start;
}

#endif
