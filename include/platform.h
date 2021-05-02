/**
 * Basic platform definitions / helpers
 */

#pragma once

static inline void* ivee_alloc(size_t size)
{
    return malloc(size);
}

static inline void* ivee_zalloc(size_t size)
{
    return calloc(size, 1);
}

static inline void ivee_free(void* ptr)
{
    free(ptr);
}
