#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#include "memory.h"
#include "mailbox.h"
#include "log.h"

#define TAG "Memory"

/**
  @brief  Map physical memory located at offset into the virtual memory space

  @param  offset Offset in physical memory
  @param  length Length of memory to map
  @retval void* - Address of mapped memory in virtual space
*/
void* memoryMapPhysical(off_t offset, size_t length)
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
void* memoryAllocateVirtual(size_t length)
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
  @brief  Calculate the physical address of a given virtual address using the process pagemap

  @param  virtual Virtual memory address to convert to physical
  @retval void* - Physical address
*/
void* memoryVirtualToPhysical(void* virtual)
{
  const size_t pageSize = sysconf(_SC_PAGE_SIZE);

  // Open the pagemap for this process
  int32_t file = open("/proc/self/pagemap", O_RDONLY);
  if (file == -1)
  {
    LOGE(TAG, "Failed to open /proc/self/pagemap. Error: %s", strerror(errno));
    return NULL;
  }

  // Calculate offset into file for the target address
  off_t offset = ((off_t)virtual / pageSize) * sizeof(pagemap_entry_t);

  LOGD(TAG, "Reading pagemap at offset 0x%X.", offset);

  pagemap_entry_t entry = (pagemap_entry_t) {0};
  ssize_t read = pread(file, &entry, sizeof(pagemap_entry_t), offset);

  int32_t result = close(file);
  if (result == -1)
    LOGW(TAG, "Failed to close /proc/self/pagemap. Error: %s", strerror(errno));

  if (read != sizeof(pagemap_entry_t))
  {
    LOGE(TAG, "Failed to read complete entry from pagemap.");
    return NULL;
  }

  assert(entry.present && !entry.swapped);

  return  (void*)((uint32_t)entry.pfn * pageSize) + ((uint32_t)virtual % pageSize);;
}

/**
  @brief  Allocate & lock physical memory via the VideoCore

  @param  length Length of memory too allocate
  @retval memory_physical_t - Mailbox handle and bus address
*/
memory_physical_t memoryAllocatePhysical(size_t length)
{
  memory_physical_t memory;
  memory.handle = -1;
  memory.address = NULL;

  // Attempt to allocate memory from the VC
  memory.handle = mailboxAllocateMemory(length, sysconf(_SC_PAGE_SIZE), MAILBOX_MEM_FLAG_DIRECT | MAILBOX_MEM_FLAG_ZERO_INIT);
  if (memory.handle < 0)
  {
    LOGE(TAG, "Failed to allocate memory of length %d via mailbox.", length);    
    return memory;
  }

  // Lock the memory to get an address
  memory.address = mailboxLockMemory(memory.handle);
  if (memory.address == NULL)
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
int32_t memoryReleasePhysical(const memory_physical_t* memory)
{
  int32_t result = mailboxUnlockMemory(memory->handle);
  if (result < 0)
  {
    LOGE(TAG, "Failed to unlock memory via mailbox.");
    return result;
  }

  result = mailboxReleaseMemory(memory->handle);
  if (result < 0)
  {
    LOGE(TAG, "Failed to release memory via mailbox.");
    return result;
  }

  return 0;
}
