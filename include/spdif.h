#ifndef __SPDIF__
#define __SPDIF__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPDIF_FRAME_COUNT 192

typedef enum spdif_preamble_t
{
  spdif_preamble_m, // 1st sub-frame, left channel
  spdif_preamble_w, // 2nd sub-frame, right channel
  spdif_preamble_b, // 1st sub-frame, left channel, start of block
} spdif_preamble_t;

typedef union spdif_pcm_channel_status_t
{
  struct
  {
    // Byte 0
    uint8_t aes3 : 1; // 0 for SPDIF
    uint8_t compressed : 1; // 0 for PCM
    uint8_t copy_permit : 1; 
    uint8_t pcm_mode : 3; // 000 2 channel no pre-emphasis  TODO
    uint8_t mode : 2; // Channel status format/mode 00
    
    // Byte 1
    uint8_t category_code;

    // Byte 2
    uint8_t source_number : 4; // 000 Do not use
    uint8_t channel_number : 4; // 1 - Left channel, 2 - Right chanel

    // Byte 3
    uint8_t sample_frequency : 4; // 0 - 44.1 kHz, 1 - Not indicated
    uint8_t clock_accuracy : 2;
    uint8_t _reserved : 2;

    // Byte 4
    uint8_t word_length : 1; // 0 - 20 bit max sample length
    uint8_t sample_word_length : 3; // 0 - Not indicated 1 - 16 bits
    uint8_t original_sampling_frequency : 4; // 0 not indicated

    uint8_t _reserved2[19];
  };
  uint8_t raw[24]; // 192 bits
} spdif_pcm_channel_status_t;

typedef union spdif_subframe_t
{
  struct
  {
    uint32_t preamble       : 4; // Preamble takes 4 bits of time but is not stored here
    uint32_t aux            : 4;
    uint32_t sample         : 20;
    uint32_t validity       : 1;
    uint32_t user_data      : 1;
    uint32_t channel_status : 1;
    uint32_t parity         : 1; // Even Parity. Set to make number of 1's even
  };
  uint32_t raw;
} spdif_subframe_t;

typedef struct spdif_frame_t
{
  spdif_subframe_t a;
  spdif_subframe_t b;
} spdif_frame_t;

typedef struct spdif_block_t
{
  spdif_frame_t frames[SPDIF_FRAME_COUNT];
} spdif_block_t;

uint64_t spdifBuildSubframe(spdif_subframe_t* subframe, spdif_preamble_t preamble, int16_t sample);

#ifdef __cplusplus
} 
#endif

#endif