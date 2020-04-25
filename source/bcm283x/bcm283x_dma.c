#include <stddef.h>
#include <assert.h>

#include "bcm283x_dma.h"
#include "bcm283x.h"

static void* dma = NULL;

/**
  @brief  Initalize the DMA object at the given base address

  @param  base Base address of DMA peripheral
  @retval none
*/
void dmaInit(void* base)
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
static bcm283x_dma_channel_t* dmaGetChannel(dma_channel_t channel)
{
  assert(dma != NULL);
  assert(channel < dma_channel_max);

  return (bcm283x_dma_channel_t*) (dma + (channel * DMA_CHANNEL_OFFSET));
}

/**
  @brief  Reset the target DMA channel

  @param  channel DMA channel number
  @retval void
*/
void dmaReset(dma_channel_t channel)
{
  bcm283x_dma_channel_t* handle = dmaGetChannel(channel);

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
void dmaSetControlBlock(dma_channel_t channel, const dma_control_block_t* control)
{
  // Ensure block is 256 bit aligned
  assert(((uint32_t)control & 0x1F) == 0);

  bcm283x_dma_channel_t* handle = dmaGetChannel(channel);

  WMB();

  handle->CONBLK_AD = (dma_control_block_t*) control;
}

/**
  @brief  Enable/disable select DMA channel

  @param  channel DMA channel number
  @param  enable Enable the channel
  @retval void
*/
void dmaEnable(dma_channel_t channel, bool enable)
{
  bcm283x_dma_channel_t* handle = dmaGetChannel(channel);

  WMB();

  handle->CS.ACTIVE = enable;
}

/**
  @brief  Test if the select DMA channel is Active/busy

  @param  channel DMA channel number
  @retval bool
*/
bool dmaActive(dma_channel_t channel)
{
  bcm283x_dma_channel_t* handle = dmaGetChannel(channel);

  bool active = handle->CS.ACTIVE;

  RMB();

  return active;
}