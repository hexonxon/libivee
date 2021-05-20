/**
 * Basic platform definitions / helpers
 */

#pragma once

#define PAGE_SHIFT  12
#define PAGE_SIZE   (1ul << PAGE_SHIFT)

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
