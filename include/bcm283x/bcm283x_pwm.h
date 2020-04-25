#ifndef __BCM283X_PWM__
#define __BCM283X_PWM__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/**
  @brief  Internal structures, types and constants
*/

#define PWM_BASE_OFFSET (0x0020C000)

typedef struct pwm_control_t
{
  uint32_t PWEN1 : 1;
  uint32_t MODE1 : 1;
  uint32_t RPTL1 : 1;
  uint32_t SBIT1 : 1;
  uint32_t POLA1 : 1;
  uint32_t USEF1 : 1;
  uint32_t CLRF1 : 1;
  uint32_t MSEN1 : 1;
  uint32_t PWEN2 : 1;
  uint32_t MODE2 : 1;
  uint32_t RPTL2 : 1;
  uint32_t SBIT2 : 1;
  uint32_t POLA2 : 1;
  uint32_t USEF2 : 1;
  uint32_t _reserved : 1;
  uint32_t MSEN2 : 1;
  uint32_t _reserved2 : 16;
} pwm_control_t;

typedef struct pwm_status_t
{
  uint32_t FULL1 : 1;
  uint32_t EMPT1 : 1;
  uint32_t WERR1 : 1;
  uint32_t RERR1 : 1;
  uint32_t GAPO1 : 1;
  uint32_t GAPO2 : 1;
  uint32_t GAPO3 : 1; // Ch 3 doesn't exist?
  uint32_t GAPO4 : 1; // Ch 4 doesn't exist?
  uint32_t BERR : 1;
  uint32_t STA1 : 1;
  uint32_t STA2 : 1;
  uint32_t STA3 : 1; // Ch 3 doesn't exist?
  uint32_t STA4 : 1; // Ch 4 doesn't exist?
  uint32_t _reserved : 18;
} pwm_status_t;

typedef struct pwm_dma_config_t
{
  uint32_t DREQ : 8;
  uint32_t PANIC : 8;
  uint32_t _reserved : 15;
  uint32_t ENAB : 1;
} pwm_dma_config_t;

typedef struct bcm283x_pwm_t
{
  volatile pwm_control_t    CTL;
  volatile pwm_status_t     STA;
  volatile pwm_dma_config_t DMAC;
  volatile uint32_t         _reserved;
  volatile uint32_t         RNG1;
  volatile uint32_t         DAT1;
  volatile uint32_t         FIF1;
  volatile uint32_t         _reserved2;
  volatile uint32_t         RNG2;
  volatile uint32_t         DAT2;
} bcm283x_pwm_t;

static_assert(sizeof(pwm_control_t) == sizeof(uint32_t), "pwm_control_t must be 4 bytes.");
static_assert(sizeof(pwm_status_t) == sizeof(uint32_t), "pwm_status_t must be 4 bytes.");
static_assert(sizeof(pwm_dma_config_t) == sizeof(uint32_t), "pwm_dma_config_t must be 4 bytes.");
static_assert(sizeof(bcm283x_pwm_t) == 10 * sizeof(uint32_t), "bcm283x_pwm_t must be 40 bytes.");

/**
  @brief  External structures, types and interfaces.
*/

typedef enum pwm_channel_t
{
  pwm_channel_1,
  pwm_channel_2,
  pwm_channel_max,
} pwm_channel_t;

typedef enum pwm_mode_t 
{
  pwm_mode_pwm_algorithm,
  pwm_mode_mark_space,
  pwm_mode_serial,
} pwm_mode_t;

typedef struct pwm_configuration_t
{
  pwm_mode_t mode;
  bool       fifoEnable;
  bool       repeatLast;
  bool       invert;
  uint8_t    silenceBit;
} pwm_configuration_t;

void pwmInit(void* base);
void pwmReset(void);
void pwmClearFifo(void);
void pwmConfigureDma(bool enable, uint32_t panicThreshold, uint32_t dreqThreshold);
void pwmSetData(pwm_channel_t channel, uint32_t data);
void pwmSetRange(pwm_channel_t channel, uint32_t data);
void pwmConfigure(pwm_channel_t channel, const pwm_configuration_t* config);
void pwmEnable(pwm_channel_t channel, bool enable);

#endif