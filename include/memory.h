#ifndef __MEMORY__
#define __MEMORY__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

typedef struct pagemap_entry_t
{
  uint64_t pfn : 55;
  uint64_t softDirty : 1;
  uint64_t exclusiveMap : 1;
  uint64_t _reserved : 4;
  uint64_t filePageSharedAnon : 1;
  uint64_t swapped : 1;
  uint64_t present : 1;
} pagemap_entry_t;

typedef struct memory_physical_t
{
  int32_t handle;
  void*   address;
} memory_physical_t;

static_assert(sizeof(pagemap_entry_t) == sizeof(uint64_t), "pagemap_entry_t must be 64 bits.");

void* memoryMapPhysical(off_t offset, size_t length);
void* memoryAllocateVirtual(size_t length);
void* memoryVirtualToPhysical(void* virtual);
memory_physical_t memoryAllocatePhysical(size_t length);
int32_t memoryReleasePhysical(const memory_physical_t* memory);

#endif