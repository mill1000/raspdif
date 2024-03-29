#include <assert.h>
#include <stddef.h>
#include <time.h>

#include "bcm283x.h"
#include "bcm283x_gpio.h"

static bcm283x_gpio_t* gpio = NULL;

/**
  @brief  Initialize the GPIO object at the given base address

  @param  base Base address of GPIO peripheral
  @retval none
*/
void bcm283x_gpio_init(void* base)
{
  assert(base != NULL);
  assert(gpio == NULL);

  gpio = base;
}

/**
  @brief  Set the GPIO pin function

  @param  pin GPIO pin to set
  @param  function GPIO function to set
  @retval none
*/
static void bcm283x_gpio_set_function(gpio_pin_t pin, gpio_function_t function)
{
  volatile gpio_function_select_x_t* func_select = &gpio->GPFSELx[pin / 10];

  switch (pin % 10)
  {
    case 0:
      func_select->FSELx0 = function;
      break;

    case 1:
      func_select->FSELx1 = function;
      break;

    case 2:
      func_select->FSELx2 = function;
      break;

    case 3:
      func_select->FSELx3 = function;
      break;

    case 4:
      func_select->FSELx4 = function;
      break;

    case 5:
      func_select->FSELx5 = function;
      break;

    case 6:
      func_select->FSELx6 = function;
      break;

    case 7:
      func_select->FSELx7 = function;
      break;

    case 8:
      func_select->FSELx8 = function;
      break;

    case 9:
      func_select->FSELx9 = function;
      break;

    default:
      assert(false);
      break;
  }
}

/**
  @brief  Configure the target GPIO pin

  @param  pin GPIO pin to configure
  @param  config GPIO configuration to set
  @retval void
*/
void bcm283x_gpio_configure(gpio_pin_t pin, const gpio_configuration_t* config)
{
  assert(gpio != NULL);
  assert(config != NULL);
  assert(pin < GPIO_PIN_COUNT);

  WMB();

  bcm283x_gpio_set_function(pin, config->function);

  // Determine which 32-bit "bank" the pin is in
  uint8_t bank = pin / 32;

  // Construct a mask for the pin number in it's bank
  uint32_t mask = 1 << (pin % 32);

  // Clear all event registers
  gpio->GPRENx[bank].REN &= ~mask;
  gpio->GPFENx[bank].FEN &= ~mask;
  gpio->GPHENx[bank].HEN &= ~mask;
  gpio->GPLENx[bank].LEN &= ~mask;
  gpio->GPARENx[bank].AREN &= ~mask;
  gpio->GPAFENx[bank].AFEN &= ~mask;

  switch (config->event_detect)
  {
    case gpio_event_detect_none:
      // Do nothing
      break;

    case gpio_event_detect_rising_edge:
      gpio->GPRENx[bank].REN |= mask;
      break;

    case gpio_event_detect_falling_edge:
      gpio->GPFENx[bank].FEN |= mask;
      break;

    case gpio_event_detect_any_edge:
      gpio->GPRENx[bank].REN |= mask;
      gpio->GPFENx[bank].FEN |= mask;
      break;

    case gpio_event_detect_high_level:
      gpio->GPHENx[bank].HEN |= mask;
      break;

    case gpio_event_detect_low_level:
      gpio->GPLENx[bank].LEN |= mask;
      break;

    case gpio_event_detect_rising_edge_async:
      gpio->GPARENx[bank].AREN |= mask;
      break;

    case gpio_event_detect_falling_edge_async:
      gpio->GPAFENx[bank].AFEN |= mask;
      break;

    default:
      break;
  }

  if (config->pull != gpio_pull_no_change)
  {
    // Set the mode bits
    gpio->GPPUD.PUD = config->pull;

    // Wait at least 150 cycles. Total guess.
    bcm283x_delay_microseconds(10);

    // Set the clock bit for target pin
    gpio->GPPUDCLKx[bank].PUDCLK |= mask;

    // Wait 150 more cycles
    bcm283x_delay_microseconds(10);

    // Clear clock
    gpio->GPPUDCLKx[bank].PUDCLK &= ~mask;
  }

  RMB();
}

/**
  @brief  Configure a group of pins indicated by the provided mask

  @param  mask GPIO pin mask to configure
  @param  config GPIO configuration to set
  @retval void
*/
void bcm283x_gpio_configure_mask(gpio_pin_mask_t mask, const gpio_configuration_t* config)
{
  gpio_pin_t pin = 0;
  while (mask)
  {
    if (mask & 0x01)
      bcm283x_gpio_configure(pin, config);

    mask >>= 1;
    pin++;
  }
}

/**
  @brief  Set the target GPIO pin

  @param  pin GPIO pin to set
  @retval void
*/
void bcm283x_gpio_set(gpio_pin_t pin)
{
  assert(gpio != NULL);
  assert(pin < GPIO_PIN_COUNT);

  // Determine which 32-bit "bank" the pin is in
  uint8_t bank = pin / 32;

  // Construct a mask for the pin number in it's bank
  uint32_t mask = 1 << (pin % 32);

  WMB();
  gpio->GPSETx[bank].SET = mask;
}

/**
  @brief  Set all GPIO pins in the input mask

  @param  mask GPIO pin mask to set
  @retval void
*/
void bcm283x_gpio_set_mask(gpio_pin_mask_t mask)
{
  assert(gpio != NULL);

  WMB();
  gpio->GPSETx[0].SET = mask;

#ifdef BCM283X_EXTENDED_GPIO
  gpio->GPSETx[1].SET = mask >> 32;
#endif
}

/**
  @brief  Clear the target GPIO pin

  @param  pin GPIO pin to set
  @retval void
*/
void bcm283x_gpio_clear(gpio_pin_t pin)
{
  assert(gpio != NULL);
  assert(pin < GPIO_PIN_COUNT);

  // Determine which 32-bit "bank" the pin is in
  uint8_t bank = pin / 32;

  // Construct a mask for the pin number in it's bank
  uint32_t mask = 1 << (pin % 32);

  WMB();
  gpio->GPCLRx[bank].CLR = mask;
}

/**
  @brief  Clear all GPIO pins in the input mask

  @param  mask GPIO pin mask to clear
  @retval void
*/
void bcm283x_gpio_clear_mask(gpio_pin_mask_t mask)
{
  assert(gpio != NULL);

  WMB();
  gpio->GPCLRx[0].CLR = mask;

#ifdef BCM283X_EXTENDED_GPIO
  gpio->GPCLRx[1].CLR = mask >> 32;
#endif
}