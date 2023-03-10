#include <stddef.h>
#include <assert.h>

#include <bcm_host.h>

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
  static void* virtualBase = NULL;

  // Don't initialize twice
  if (virtualBase != NULL)
  {
    LOGW(TAG, "Already initialized.");
    return;
  }

  // Fetch physical address and length of peripherals for our system
  off_t physicalBase = bcm_host_get_peripheral_address();
  size_t length = bcm_host_get_peripheral_size();

  // Map to virtual memory
  virtualBase = memory_map_physical(physicalBase, length);
  if (virtualBase == NULL)
  {
    LOGF(TAG, "Failed to map physical memory.");
    return;
  }

  // Initialize modules at their base addresses
  bcm283x_clock_init(virtualBase + CLOCK_BASE_OFFSET);
  bcm283x_gpio_init(virtualBase + GPIO_BASE_OFFSET);
  bcm283x_dma_init(virtualBase + DMA_BASE_OFFSET);
  bcm283x_pcm_init(virtualBase + PCM_BASE_OFFSET);
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