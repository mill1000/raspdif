#ifndef __PIPWM__
#define __PIPWM__

#include "bcm283x_dma.h"
#include "bcm283x_gpio.h"
#include "memory.h"

#define PIPWM_SOURCE_CLOCK_HZ (19.2e6)
#define PIPWM_MAX_STEP_COUNT  (UINT16_MAX / sizeof(uint32_t))
#define PIPWM_MIN_STEP_COUNT  (2)
#define PIPWM_MIN_TIME_RESOLUTION (5e-6)

typedef struct pipwm_control_t
{
  dma_control_block_t controlBlocks[2];
  gpio_pin_mask_t     setMask;
  gpio_pin_mask_t     clearMask[1]; // Actually N - 1, where N is channel resolution
} pipwm_control_t;

typedef struct pipwm_channel_t
{
  dma_channel_t     dmaChannel;
  gpio_pin_mask_t   pinMask;
  memory_physical_t memory;
  pipwm_control_t*  vControl;
  uint32_t          steps;
} pipwm_channel_t;

double piPwm_initialize(dma_channel_t dmaChannel, double resolution_s);
void piPwm_shutdown(void);
pipwm_channel_t* piPwm_initalizeChannel(dma_channel_t dmaChannel, gpio_pin_mask_t pinMask, double frequency_Hz);
void piPwm_releaseChannel(pipwm_channel_t* channel);
void piPwm_enableChannel(const pipwm_channel_t* channel, bool enable);
void piPwm_setDutyCycle(pipwm_channel_t* channel, gpio_pin_mask_t mask, double dutyCycle);
void piPwm_setPulseWidth(pipwm_channel_t* channel, gpio_pin_mask_t mask, double pulseWidth_s);
#endif