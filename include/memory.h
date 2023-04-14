#ifndef __MEMORY__
#define __MEMORY__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct pagemap_entry_t
{
  uint64_t pfn                   : 55;
  uint64_t soft_dirty            : 1;
  uint64_t exclusive_map         : 1;
  uint64_t _reserved             : 4;
  uint64_t file_page_shared_anon : 1;
  uint64_t swapped               : 1;
  uint64_t present               : 1;
} pagemap_entry_t;

typedef struct memory_physical_t
{
  int32_t handle;
  void* address;
} memory_physical_t;

static_assert(sizeof(pagemap_entry_t) == sizeof(uint64_t), "pagemap_entry_t must be 64 bits.");

void* memory_map_physical(off_t offset, size_t length);
void* memory_allocate_virtual(size_t length);
memory_physical_t memory_allocate_physical(size_t length);
int32_t memory_release_physical(const memory_physical_t* memory);

#endif