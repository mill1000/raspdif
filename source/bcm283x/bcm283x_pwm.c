#include <stddef.h>
#include <assert.h>

#include "bcm283x_pwm.h"
#include "bcm283x.h"

static bcm283x_pwm_t* pwm = NULL;

/**
  @brief  Initalize the PWM object at the given base address

  @param  base Base address of PWM peripheral
  @retval none
*/
void pwmInit(void* base)
{
  assert(base != NULL);
  assert(pwm == NULL);

  pwm = base;
}

/**
  @brief  Set the range of the target PWM channel

  @param  channel PWM channel
  @param  range Range value to set
  @retval none
*/
void pwmSetRange(pwm_channel_t channel, uint32_t range)
{
  assert(pwm != NULL);
  assert(channel < pwm_channel_max);
  
  WMB();
  
  switch (channel)
  {
    case pwm_channel_1:
      pwm->RNG1 = range;
      break;

    case pwm_channel_2:
      pwm->RNG2 = range;
      break;

    default:
      break;
  }
}

/**
  @brief  Set the data of the target PWM channel

  @param  channel PWM channel
  @param  data Data value to set
  @retval none
*/
void pwmSetData(pwm_channel_t channel, uint32_t data)
{
  assert(pwm != NULL);
  assert(channel < pwm_channel_max);

  WMB();

  switch (channel)
  {
    case pwm_channel_1:
      pwm->DAT1 = data;
      break;

    case pwm_channel_2:
      pwm->DAT2 = data;
      break;

    default:
      break;
  }
}

/**
  @brief  Configure the PWM DMA settings

  @param  enable Enable DMA requests from the PWM peripheral
  @param  panicThreshold Threshold for PANIC signal
  @param  dreqThreshold Threshold for DREQ signal
  @retval none
*/
void pwmConfigureDma(bool enable, uint32_t panicThreshold, uint32_t dreqThreshold)
{
  assert(pwm != NULL);

  WMB();

  pwm->DMAC.DREQ = dreqThreshold;  
  pwm->DMAC.PANIC = panicThreshold;
  pwm->DMAC.ENAB = enable;
}

/**
  @brief  Reset the PWM peripheral

  @param  none
  @retval none
*/
void pwmReset()
{
  assert(pwm != NULL);

  WMB();

  pwm->CTL = (pwm_control_t) {0};

  // Clear error flags
  pwm->STA.WERR1 = 1;
  pwm->STA.RERR1 = 1;
  pwm->STA.BERR = 1;

  pwm->DMAC.ENAB = 0;
  pwm->DMAC.PANIC = 0x7;
  pwm->DMAC.DREQ = 0x7;

  pwm->DAT1 = 0;
  pwm->RNG1 = 0;
  
  pwm->DAT2 = 0;
  pwm->RNG2 = 0;
}

/**
  @brief  Clear the PWM FIFO

  @param  none
  @retval none
*/
void pwmClearFifo()
{
  assert(pwm != NULL);

  WMB();

  pwm->CTL.CLRF1 = 1;
}

/**
  @brief  Configure the target PWM channel

  @param  peripheral Target peripheral clock to configure
  @param  config PWM configuration to set
  @retval void
*/
void pwmConfigure(pwm_channel_t channel, const pwm_configuration_t* config)
{
  assert(pwm != NULL);
  assert(config != NULL);
  
  WMB();

  // Update control register according to config
  if (channel == pwm_channel_1)
  {
    // Disable channel before update
    pwm->CTL.PWEN1 = 0;

    bcm283x_delay_microseconds(10);

    // Create a copy of the control register to modify
    pwm_control_t control = pwm->CTL;

    switch(config->mode)
    {
      case pwm_mode_serial:
        control.MODE1 = 1;
        control.MSEN1 = 0; // Don't care
      break;

      case pwm_mode_mark_space:
        control.MODE1 = 0;
        control.MSEN1 = 1;
      break;

      case pwm_mode_pwm_algorithm:
        control.MODE1 = 0;
        control.MSEN1 = 0;
      break;
    }

    control.USEF1 = config->fifoEnable;
    control.RPTL1 = config->repeatLast;
    control.POLA1 = config->invert;
    control.SBIT1 = config->silenceBit;

    pwm->CTL = control;
  }
  else
  {
    // Disable channel before update
    pwm->CTL.PWEN2 = 0;

    bcm283x_delay_microseconds(10);

    // Create a copy of the control register to modify
    pwm_control_t control = pwm->CTL;

    switch(config->mode)
    {
      case pwm_mode_serial:
        control.MODE2 = 1;
        control.MSEN2 = 0; // Don't care
      break;

      case pwm_mode_mark_space:
        control.MODE2 = 0;
        control.MSEN2 = 1;
      break;

      case pwm_mode_pwm_algorithm:
        control.MODE2 = 0;
        control.MSEN2 = 0;
      break;
    }

    control.USEF2 = config->fifoEnable;
    control.RPTL2 = config->repeatLast;    
    control.POLA2 = config->invert;
    control.SBIT2 = config->silenceBit;

    pwm->CTL = control;
  }

  RMB();

  bcm283x_delay_microseconds(10);
}

/**
  @brief  Enable or disable the target PWM channel

  @param  channel PWM channel to enable
  @param  enable Enable the channel
  @retval void
*/
void pwmEnable(pwm_channel_t channel, bool enable)
{
  assert(pwm != NULL);

  WMB();

  // Set target channel enabel bit
  if (channel == pwm_channel_1)
    pwm->CTL.PWEN1 = enable;
  else
    pwm->CTL.PWEN2 = enable;
}
