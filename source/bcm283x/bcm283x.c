#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "bcm283x.h"
#include "log.h"
#include "memory.h"
#include "utils.h"

#define TAG "BCM283X"

/**
  @brief  Initialize BCM283X peripheral modules

  @param  none
  @retval none
*/
void bcm283x_init()
{
  static void* virtual_base = NULL;

  // Don't initialize twice
  if (virtual_base != NULL)
  {
    LOGW(TAG, "Already initialized.");
    return;
  }

  // Fetch physical address and length of peripherals for our system
  off_t physical_base = bcm283x_get_peripheral_address();
  size_t length = bcm283x_get_peripheral_size();

  // Map to virtual memory
  virtual_base = memory_map_physical(physical_base, length);
  if (virtual_base == NULL)
  {
    LOGF(TAG, "Failed to map physical memory.");
    return;
  }

  // Initialize modules at their base addresses
  bcm283x_clock_init(virtual_base + CLOCK_BASE_OFFSET);
  bcm283x_gpio_init(virtual_base + GPIO_BASE_OFFSET);
  bcm283x_dma_init(virtual_base + DMA_BASE_OFFSET);
  bcm283x_pcm_init(virtual_base + PCM_BASE_OFFSET);
}

/**
  @brief  Delay for the target number of microseconds.
          Primarily a helper function to prevent peripheral drivers
          from directly calling POSIX functions

  @param  microseconds Delay time in microseconds
  @retval none
*/
void bcm283x_delay_microseconds(uint32_t microseconds)
{
  microsleep(microseconds);
}

/**
  @brief  Fetch address information from the device tree

  @param  filename Device tree node
  @param  offset Offset in node to read
  @retval uintptr_t
*/
static uintptr_t bcm283x_get_dt_ranges(const char* filename, off_t offset)
{
  // Open node
  FILE* fp = fopen(filename, "rb");
  if (fp == NULL)
    return UINTPTR_MAX;

  // Seek to offset
  fseek(fp, offset, SEEK_SET);

  // Read range from node
  uintptr_t address = UINTPTR_MAX;
  unsigned char buffer[4];
  if (fread(buffer, 1, sizeof(buffer), fp) == sizeof(buffer))
    address = buffer[0] << 24 | buffer[1] << 16 | buffer[2] << 8 | buffer[3] << 0;

  // Cleanup
  fclose(fp);

  return address;
}

/**
  @brief  Get the peripheral base address from the device tree

  @param  none
  @retval uintptr_t
*/
uintptr_t bcm283x_get_peripheral_address(void)
{
  uintptr_t address = bcm283x_get_dt_ranges("/proc/device-tree/soc/ranges", 4);
  if (address == 0)
    address = bcm283x_get_dt_ranges("/proc/device-tree/soc/ranges", 8);
  return address == UINTPTR_MAX ? 0x20000000 : address;
}

/**
  @brief  Get the peripheral address size from the device tree

  @param  none
  @retval uintptr_t
*/
uintptr_t bcm283x_get_peripheral_size(void)
{
  uintptr_t address = bcm283x_get_dt_ranges("/proc/device-tree/soc/ranges", 4);
  address = bcm283x_get_dt_ranges("/proc/device-tree/soc/ranges", (address == 0) ? 12 : 8);
  return address == UINTPTR_MAX ? 0x01000000 : address;
}

/**
  @brief  Get the physical address of the SDRAM from the device tree

  @param  none
  @retval uintptr_t
*/
uintptr_t bcm283x_get_sdram_address(void)
{
  uintptr_t address = bcm283x_get_dt_ranges("/proc/device-tree/axi/vc_mem/reg", 8);
  return address == UINTPTR_MAX ? 0x40000000 : address;
}

/**
  @brief  Read a string from the given file

  @param  filename Filename to read
  @param  format String format to extract
  @param  value Storage for extracted value
  @retval bool
*/
static bool bcm283x_get_string_from_file(const char* filename, const char* format, uint32_t* value)
{
  // Open the file
  FILE* file = fopen(filename, "rt");
  if (file == NULL)
    return false;

  char str[256] = {0};
  bool found = false;
  while (fgets(str, sizeof(str), file) != NULL)
  {
    if (sscanf(str, format, value) == 1)
    {
      found = true;
      break;
    }
  }

  fclose(file);

  return found;
}

/**
  @brief  Get the processor revision code

  @param  none
  @retval uint32_t
*/
static uint32_t bcm283x_get_revision_code()
{
  static uint32_t revision_num = UINT32_MAX;

  if (revision_num != UINT32_MAX)
    return revision_num;

  uint32_t num = UINT32_MAX;
  if (bcm283x_get_string_from_file("/proc/cpuinfo", "Revision : %x", &num))
    revision_num = num;

  return revision_num;
}

/**
  @brief  Get the processor ID

  @param  none
  @retval uint32_t
*/
static uint32_t bcm283x_get_processor_id(void)
{
  uint32_t revision_num = bcm283x_get_revision_code();

  if (revision_num & 0x800000)
    return (revision_num & 0xf000) >> 12;

  // Old style number only used 2835
  return BCM283X_PROCESSOR_BCM2835;
}

/**
  @brief  Check if the system is a Pi 4

  @param  none
  @retval bool
*/
bool bcm283x_is_model_pi4(void)
{
  return bcm283x_get_processor_id() == BCM283X_PROCESSOR_BCM2711;
}
