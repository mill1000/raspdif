#ifndef __BCM283X__
#define __BCM283X__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// Enable GPIOs 32 - 54
//#define BCM283X_EXTENDED_GPIO

#define BCM283X_BUS_PERIPHERAL_BASE (0x7E000000)

// Use GCC internal __sync functions until we know how to do this better
#define WMB() do {__sync_synchronize();} while (0)
#define RMB() WMB()

void bcm283x_init(void);
void bcm283x_delay_microseconds(uint32_t microseconds);

#include "bcm283x_clock.h"
#include "bcm283x_dma.h"
#include "bcm283x_gpio.h"
#include "bcm283x_mailbox.h"
#include "bcm283x_pcm.h"

#endif