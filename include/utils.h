#ifndef __UTILS__
#define __UTILS__

#include <time.h>
#include <assert.h>
#include <stdint.h>

static inline void microsleep(uint32_t microseconds)
{
  assert(microseconds < 1e6);

  struct timespec delay;
  delay.tv_sec = 0;
  delay.tv_nsec = 1e3 * microseconds;

  nanosleep(&delay, NULL);
}

#endif