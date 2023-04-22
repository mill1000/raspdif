#include <arm_acle.h>
#include <string.h>

#include "spdif.h"

#define TAG "SPDIF"

// Inverted if preceding bit state was 1
// Which shouldn't happen due to even parity
#define SPDIF_PREAMBLE_M 0xE2 // Sub-frame 1
#define SPDIF_PREAMBLE_W 0xE4 // Sub-frame 2
#define SPDIF_PREAMBLE_B 0xE8 // Sub-frame 1, start of block

/**
  @brief  Encode the provided 32 bit data into BMC with the provided preamble

  @param  preamble Preamble type for this subframe
  @param  data 32 bit sub-frame data to encode
  @retval uint64_t - BMC encoded subframe
*/
static uint64_t spdif_encode_biphase_mark(spdif_preamble_t preamble, uint32_t data)
{
  // Biphase Mark
  // Each bit to be transmitted is 2 binary states
  // 1st is always different from previous, 2nd is identical if 0, different if 1
  // Assuming previous was 0
  // 0000 -> 1100 1100
  // 0001 -> 1100 1101
  // 0010 -> 1100 1011
  // 0011 -> 1100 1010

  // 0100 -> 1101 0011
  // 0101 -> 1101 0010
  // 0110 -> 1101 0100
  // 0111 -> 1101 0101

  // 1000 -> 1011 0011
  // 1001 -> 1011 0010
  // 1010 -> 1011 0100
  // 1011 -> 1011 0101

  // 1100 -> 1010 1100
  // 1101 -> 1010 1101
  // 1110 -> 1010 1011
  // 1111 -> 1010 1010

  // Example of first subframe
  // Ignored    | Aux       | Sample                              | Valid | User | Status | Parity
  // 0000       | 0000      | 0000 0000 0000 0000 0000            | 1     | 0    | 0      | 1
  // 11101000   | 1100 1100 | 11001100 11001100 11001100 11001100 | 10    | 11   | 00     | 10

  // LUT to BMC encode nibbles
  // Invert if last state was 1
  // clang-format off
  static uint8_t bmc_lut_0[16] = 
  {
    0xCC, 0xCD, 0xCB, 0xCA,
    0xD3, 0xD2, 0xD4, 0xD5,
    0xB3, 0xB2, 0xB4, 0xB5,
    0xAC, 0xAD, 0xAB, 0xAA,
  };
  // clang-format on

  union
  {
    uint8_t byte[8];
    uint64_t raw;
  } bmc;

  // Set preamble bits
  switch (preamble)
  {
    case spdif_preamble_b:
      bmc.byte[7] = SPDIF_PREAMBLE_B;
      break;

    case spdif_preamble_m:
      bmc.byte[7] = SPDIF_PREAMBLE_M;
      break;

    case spdif_preamble_w:
      bmc.byte[7] = SPDIF_PREAMBLE_W;
      break;
  }

  // Encode data a nibble at a time
  // Code is inversted if previous state was 1
  // Aux Data
  bmc.byte[6] = bmc_lut_0[(data >> 24) & 0xF]; // No need to check last state, preamble guarantees 0
  // Sample
  bmc.byte[5] = bmc_lut_0[(data >> 20) & 0xF] ^ -(int)(bmc.byte[6] & 1); // Branchless inversion yo!
  bmc.byte[4] = bmc_lut_0[(data >> 16) & 0xF] ^ -(int)(bmc.byte[5] & 1);
  bmc.byte[3] = bmc_lut_0[(data >> 12) & 0xF] ^ -(int)(bmc.byte[4] & 1);
  bmc.byte[2] = bmc_lut_0[(data >> 8) & 0xF] ^ -(int)(bmc.byte[3] & 1);
  bmc.byte[1] = bmc_lut_0[(data >> 4) & 0xF] ^ -(int)(bmc.byte[2] & 1);
  // Valid, User, Status, Parity
  bmc.byte[0] = bmc_lut_0[(data >> 0) & 0xF] ^ -(int)(bmc.byte[1] & 1);

  return bmc.raw;
}

/**
  @brief  Update and encode a SPDIF subframe with the provided sample and preamble

  @param  subframe SPDIF subframe buffer with channel status data
  @param  preamble Preamble type for this subframe
  @param  depth Bit depth of sample
  @param  sample PCM audio sample
  @retval uint64_t - BMC encoded subframe
*/
uint64_t spdif_build_subframe(spdif_subframe_t* subframe, spdif_preamble_t preamble, spdif_sample_depth_t depth, int32_t sample)
{
  switch (depth)
  {
    case spdif_sample_depth_16:
      subframe->sample = sample << 4; // Scale to 20 bits
      subframe->aux = 0;
      break;

    case spdif_sample_depth_20:
      subframe->sample = sample;
      subframe->aux = 0;
      break;

    case spdif_sample_depth_24:
      subframe->sample = sample >> 4; // MSB store in sample
      subframe->aux = sample & 0xF;   // LSB is aux data
      break;
  }

  subframe->validity = 0; // 0 Indicates OK. Dumb
  subframe->parity = 0;   // Reset parity before calculating
  subframe->parity = __builtin_popcount(subframe->raw) % 2;

  // Encode to biphase mark. PCM peripheral transmits MSBit first so bitflip data
  return spdif_encode_biphase_mark(preamble, __rbit(subframe->raw));
}

/**
  @brief  Populate the SPDIF block with channel status data

  @param  block SPDIF block to populate
  @retval none
*/
void spdif_populate_channel_status(spdif_block_t* block)
{
  // Define the SPDIF channel status data
  spdif_pcm_channel_status_t channel_status_a;
  memset(&channel_status_a, 0, sizeof(spdif_pcm_channel_status_t));

  channel_status_a.aes3 = 0;        // SPDIF aka consumer use
  channel_status_a.compressed = 0;  // PCM
  channel_status_a.copy_permit = 1; // No copy protection
  channel_status_a.pcm_mode = 0;    // 2 channel, no pre-emphasis
  channel_status_a.mode = 0;

  channel_status_a.category_code = 0; // General

  channel_status_a.source_number = 0;  // Not indicated
  channel_status_a.channel_number = 1; // Left channel

  channel_status_a.sample_frequency = 1; // Not indicated
  channel_status_a.clock_accuracy = 0;   // Level 2 TODO What is L2?

  channel_status_a.word_length = 0;                 // Max sample length is 20 bits. TODO If we do S24LE this should probably change?
  channel_status_a.sample_word_length = 0;          // Not indicated
  channel_status_a.original_sampling_frequency = 0; // Not indicated

  // Duplicate channel status for B and update channel number
  spdif_pcm_channel_status_t channel_status_b = channel_status_a;
  channel_status_b.channel_number = 2;

  // Copy channel status into each frame
  for (uint8_t i = 0; i < SPDIF_FRAME_COUNT; i++)
  {
    spdif_frame_t* frame = &block->frames[i];
    frame->a.channel_status = channel_status_a.raw[i / 8] >> (i % 8);
    frame->b.channel_status = channel_status_b.raw[i / 8] >> (i % 8);
  }
}