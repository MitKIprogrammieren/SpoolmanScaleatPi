#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <malloc.h>

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

typedef struct {
  size_t total_free_bytes;
  size_t total_allocated_bytes;
  size_t largest_free_block;
  size_t minimum_free_bytes;
  size_t allocated_blocks;
  size_t free_blocks;
  size_t total_blocks;
} multi_heap_info_t;

// The Pi has one heap. "Internal" and "SPIRAM" both come from malloc; the
// free sizes report a large, steady figure so budgets sized for the ESP pass.
inline void* heap_caps_malloc(size_t n, uint32_t) { return malloc(n); }
inline void* heap_caps_calloc(size_t c, size_t n, uint32_t) { return calloc(c, n); }
inline void* heap_caps_realloc(void* p, size_t n, uint32_t) { return realloc(p, n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_free_size(uint32_t) { return 64u * 1024 * 1024; }
inline size_t heap_caps_get_total_size(uint32_t) { return 128u * 1024 * 1024; }
inline size_t heap_caps_get_minimum_free_size(uint32_t) { return 64u * 1024 * 1024; }
inline size_t heap_caps_get_largest_free_block(uint32_t) { return 32u * 1024 * 1024; }
inline size_t heap_caps_get_allocated_size(void* p) { return p ? malloc_usable_size(p) : 0; }
inline void heap_caps_get_info(multi_heap_info_t* i, uint32_t) {
  i->total_free_bytes = 64u * 1024 * 1024;
  i->total_allocated_bytes = 0;
  i->largest_free_block = 32u * 1024 * 1024;
  i->minimum_free_bytes = 64u * 1024 * 1024;
  i->allocated_blocks = 0;
  i->free_blocks = 1;
  i->total_blocks = 1;
}
inline bool heap_caps_check_integrity_all(bool) { return true; }
