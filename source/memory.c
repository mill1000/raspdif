#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "log.h"
#include "mailbox.h"
#include "memory.h"

#define TAG "Memory"

/**
  @brief  Map physical memory located at offset into the virtual memory space

  @param  offset Offset in physical memory
  @param  length Length of memory to map
  @retval void* - Address of mapped memory in virtual space
*/
void* memory_map_physical(off_t offset, size_t length)
{
  int32_t file = open("/dev/mem", O_RDWR | O_SYNC);
  if (file == -1)
  {
    LOGF(TAG, "Failed to open /dev/mem. Error: %s", strerror(errno));
    return NULL;
  }

  // Map the physical memory (via /dev/mem) located at offset into our virtual memory
  void* virtual = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, file, offset);
  if (virtual == MAP_FAILED)
  {
    LOGF(TAG, "Failed to map physical address 0x%X of length %d. Error: %s", offset, length, strerror(errno));
    return NULL;
  }

  int32_t result = close(file);
  if (result == -1)
    LOGE(TAG, "Failed to close /dev/mem. Error: %s", strerror(errno));

  LOGD(TAG, "Mapped physical address 0x%X to virtual address 0x%X", offset, virtual);

  return virtual;
}

/**
  @brief  Allocate virtual memory via mmap

  @param  length Length of memory too allocate
  @retval void* - Address of allocated memory in virtual space
*/
void* memory_allocate_virtual(size_t length)
{
  // Allocate a block of virtual memory
  void* virtual = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS | MAP_LOCKED | MAP_NORESERVE, -1, 0);
  if (virtual == MAP_FAILED)
  {
    LOGE(TAG, "Failed to allocate virtual memory of length %d. Error: %s", length, strerror(errno));
    return NULL;
  }

  LOGD(TAG, "Allocated virtual memory at 0x%X of length %d.", virtual, length);

  return virtual;
}

/**
  @brief  Allocate & lock physical memory via the VideoCore

  @param  length Length of memory too allocate
  @retval memory_physical_t - Mailbox handle and bus address
*/
memory_physical_t memory_allocate_physical(size_t length)
{
  memory_physical_t memory = {
    .handle = -1,
    .address = PTR32_NULL,
  };

  // Attempt to allocate memory from the VC
  memory.handle = mailbox_allocate_memory(length, sysconf(_SC_PAGE_SIZE), MAILBOX_MEM_FLAG_DIRECT | MAILBOX_MEM_FLAG_ZERO_INIT);
  if (memory.handle < 0)
  {
    LOGE(TAG, "Failed to allocate memory of length %d via mailbox.", length);
    return memory;
  }

  // Lock the memory to get an address
  memory.address = mailbox_lock_memory(memory.handle);
  if (memory.address == PTR32_NULL)
  {
    LOGE(TAG, "Failed to lock memory via mailbox.", length);
    return memory;
  }

  LOGD(TAG, "Allocated memory at 0x%X of length %d.", memory.address, length);

  return memory;
}

/**
  @brief  Release physical memory allocated from the VideoCore

  @param  memory Memory struct of previously allocated physical memory
  @retval int32_t - Result of mailbox calls. 0 - success
*/
int32_t memory_release_physical(const memory_physical_t* memory)
{
  int32_t result = mailbox_unlock_memory(memory->handle);
  if (result < 0)
  {
    LOGE(TAG, "Failed to unlock memory via mailbox.");
    return result;
  }

  result = mailbox_release_memory(memory->handle);
  if (result < 0)
  {
    LOGE(TAG, "Failed to release memory via mailbox.");
    return result;
  }

  return 0;
}
