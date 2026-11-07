#ifndef __RASPDIF__
#define __RASPDIF__

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#define RASPDIF_DEFAULT_SAMPLE_RATE 44.1e3 // 44.1 kHz
#define RASPDIF_DEFAULT_FORMAT      raspdif_format_s16le
// The ring has COUNT buffers of SIZE frames; the lead (buffers kept queued
// ahead of the DMA) sets the output latency, about lead * SIZE / sample rate.
// All three can be overridden at build time via CFLAGS (not CPPFLAGS, which
// would replace the include paths), e.g.
//   make CFLAGS="-Wall -Wno-missing-braces -DRASPDIF_BUFFER_SIZE=64 -DRASPDIF_BUFFER_COUNT=32 -DRASPDIF_DEFAULT_LEAD=4"
#ifndef RASPDIF_BUFFER_COUNT
#define RASPDIF_BUFFER_COUNT 3 // Number of entries in the circular buffer
#endif
#ifndef RASPDIF_BUFFER_SIZE
#define RASPDIF_BUFFER_SIZE 2048 // Number of samples in each buffer entry. 128 (coded) bits per sample
#endif
// Buffers kept queued ahead of the DMA unless --lead says otherwise. With many
// small buffers (e.g. COUNT 32, SIZE 64) a lead of 4 gives ~4-5 ms.
// The lead may be at most COUNT - 2: the buffer being transmitted and the one
// the writer fills next must never be the same.
#ifndef RASPDIF_DEFAULT_LEAD
#define RASPDIF_DEFAULT_LEAD (RASPDIF_BUFFER_COUNT - 2)
#endif
static_assert(RASPDIF_BUFFER_COUNT >= 3, "The ring needs at least 3 buffers.");
static_assert(RASPDIF_DEFAULT_LEAD >= 1 && RASPDIF_DEFAULT_LEAD <= RASPDIF_BUFFER_COUNT - 2,
              "RASPDIF_DEFAULT_LEAD must be 1..RASPDIF_BUFFER_COUNT-2.");

typedef enum raspdif_format_t
{
  raspdif_format_s16le, // Signed 16 bit little endian
  raspdif_format_s24le, // Signed 24 bit little endian
} raspdif_format_t;

typedef struct raspdif_sample_t
{
  uint32_t msb;
  uint32_t lsb;
} raspdif_sample_t;

typedef struct raspdif_buffer_t
{
  struct
  {
    raspdif_sample_t a;
    raspdif_sample_t b;
  } sample[RASPDIF_BUFFER_SIZE];
} raspdif_buffer_t;
static_assert(sizeof(raspdif_buffer_t) <= UINT16_MAX, "SPDIF buffer must be representable in 16 bits.");

typedef struct raspdif_control_t
{
  dma_control_block_t control_blocks[RASPDIF_BUFFER_COUNT];
  raspdif_buffer_t buffers[RASPDIF_BUFFER_COUNT];
} raspdif_control_t;

#endif