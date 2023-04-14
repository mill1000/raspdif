#include <assert.h>
#include <stddef.h>

#include "bcm283x.h"
#include "bcm283x_dma.h"

static void* dma = NULL;

/**
  @brief  Initialize the DMA object at the given base address

  @param  base Base address of DMA peripheral
  @retval none
*/
void bcm283x_dma_init(void* base)
{
  assert(base != NULL);
  assert(dma == NULL);

  dma = base;
}

/**
  @brief  Fetch the channel registers for the supplied channel index.

  @param  channel DMA channel number
  @retval bcm283x_dma_channel_t*
*/
static bcm283x_dma_channel_t* bcm283x_dma_get_channel(dma_channel_t channel)
{
  assert(dma != NULL);
  assert(channel < dma_channel_max);

  return (bcm283x_dma_channel_t*)(dma + (channel * DMA_CHANNEL_OFFSET));
}

/**
  @brief  Reset the target DMA channel

  @param  channel DMA channel number
  @retval void
*/
void bcm283x_dma_reset(dma_channel_t channel)
{
  bcm283x_dma_channel_t* handle = bcm283x_dma_get_channel(channel);

  WMB();
  handle->CS.RESET = 1;

  // Clear interupt and end status flags
  handle->CS.INT = 1;
  handle->CS.END = 1;

  // Clear error flags in debug register
  handle->DEBUG.READ_ERROR = 1;
  handle->DEBUG.FIFO_ERROR = 1;
  handle->DEBUG.READ_LAST_NOT_SET_ERROR = 1;
}

/**
  @brief  Set the control block for the selected DMA channel

  @param  channel DMA channel number
  @param  control DMA control block to set on channel
  @retval void
*/
void bcm283x_dma_set_control_block(dma_channel_t channel, const dma_control_block_t* control)
{
  // Ensure block is 256 bit aligned
  assert(((uintptr_t)control & 0x1F) == 0);

  bcm283x_dma_channel_t* handle = bcm283x_dma_get_channel(channel);

  WMB();

  handle->CONBLK_AD = (uint32_t)(uintptr_t)control;
}

/**
  @brief  Get the active control block for the selected DMA channel

  @param  channel DMA channel number
  @retval dma_control_block_t* - Bus address of active control block
*/
const dma_control_block_t* bcm283x_dma_get_control_block(dma_channel_t channel)
{
  bcm283x_dma_channel_t* handle = bcm283x_dma_get_channel(channel);

  const dma_control_block_t* control = (const dma_control_block_t*)(uintptr_t)handle->CONBLK_AD;

  RMB();

  return control;
}

/**
  @brief  Enable/disable select DMA channel

  @param  channel DMA channel number
  @param  enable Enable the channel
  @retval void
*/
void bcm283x_dma_enable(dma_channel_t channel, bool enable)
{
  bcm283x_dma_channel_t* handle = bcm283x_dma_get_channel(channel);

  WMB();

  handle->CS.ACTIVE = enable;
}

/**
  @brief  Test if the select DMA channel is Active/busy

  @param  channel DMA channel number
  @retval bool
*/
bool bcm283x_dma_active(dma_channel_t channel)
{
  bcm283x_dma_channel_t* handle = bcm283x_dma_get_channel(channel);

  bool active = handle->CS.ACTIVE;

  RMB();

  return active;
}