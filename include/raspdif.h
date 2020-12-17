#ifndef __RASPDIF__
#define __RASPDIF__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define RASPDIF_DEFAULT_SAMPLE_RATE 44.1e3 // 44.1 kHz
#define RASPDIF_DEFAULT_FORMAT raspdif_format_s16le
#define RASPDIF_BUFFER_COUNT 3      // Number of entries in the circular buffer
#define RASPDIF_BUFFER_SIZE  2048   // Number of samples in each buffer entry. 128 (coded) bits per sample

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
static_assert(sizeof(raspdif_buffer_t) <=  UINT16_MAX, "SPDIF buffer must be representable in 16 bits.");

typedef struct raspdif_control_t
{
  dma_control_block_t controlBlocks[RASPDIF_BUFFER_COUNT];
  raspdif_buffer_t    buffers[RASPDIF_BUFFER_COUNT];
} raspdif_control_t;

#endif