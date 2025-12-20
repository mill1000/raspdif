#ifndef __BCM283X__
#define __BCM283X__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define BCM283X_PROCESSOR_BCM2835 0
#define BCM283X_PROCESSOR_BCM2836 1
#define BCM283X_PROCESSOR_BCM2837 2
#define BCM283X_PROCESSOR_BCM2838 3 // Deprecated name
#define BCM283X_PROCESSOR_BCM2711 3

// Enable GPIOs 32 - 54
// #define BCM283X_EXTENDED_GPIO

#define BCM283X_BUS_PERIPHERAL_BASE (0x7E000000)

// Use GCC internal __sync functions until we know how to do this better
#define WMB()             \
  do {                    \
    __sync_synchronize(); \
  } while (0)
#define RMB() WMB()

void bcm283x_init(void);
void bcm283x_delay_microseconds(uint32_t microseconds);
uint32_t bcm283x_get_peripheral_address(void);
uint32_t bcm283x_get_peripheral_size(void);
uint32_t bcm283x_get_sdram_address(void);
bool bcm283x_is_model_pi4(void);

#include "bcm283x_clock.h"
#include "bcm283x_dma.h"
#include "bcm283x_gpio.h"
#include "bcm283x_mailbox.h"
#include "bcm283x_pcm.h"

#endif