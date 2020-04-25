#include <arm_acle.h>

#include "spdif.h"

#define TAG "SPDIF"

// Inverted if preceding state was 1
// Which shouldn't happen due to even parity
#define SPDIF_PREAMBLE_M 0xE2 // Sub-frame 1
#define SPDIF_PREAMBLE_W 0xE4 // Sub-frame 2
#define SPDIF_PREAMBLE_B 0xE8 // Sub-frame 1, start of block

static uint64_t spdifEncodeBiphaseMark(spdif_preamble_t preamble, uint32_t data)
{
  // Biphase Marck
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
  static uint8_t bmcLut0[16] = 
  {
    0xCC, 0xCD, 0xCB, 0xCA,
    0xD3, 0xD2, 0xD4, 0xD5,
    0xB3, 0xB2, 0xB4, 0xB5,
    0xAC, 0xAD, 0xAB, 0xAA,
  };

  union
  {
    uint8_t  byte[8];
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
  bmc.byte[6] = bmcLut0[(data >> 24) & 0xF]; // No need to check last state, preamble guaentees 0
  // Sample
  bmc.byte[5] = bmcLut0[(data >> 20) & 0xF] ^ -(int)(bmc.byte[6] & 1); // Branchless inversion yo
  bmc.byte[4] = bmcLut0[(data >> 16) & 0xF] ^ -(int)(bmc.byte[5] & 1);
  bmc.byte[3] = bmcLut0[(data >> 12) & 0xF] ^ -(int)(bmc.byte[4] & 1);
  bmc.byte[2] = bmcLut0[(data >> 8) & 0xF] ^  -(int)(bmc.byte[3] & 1);
  bmc.byte[1] = bmcLut0[(data >> 4) & 0xF] ^  -(int)(bmc.byte[2] & 1);
  // Valid, User, Status, Parity
  bmc.byte[0] = bmcLut0[(data >> 0) & 0xF] ^  -(int)(bmc.byte[1] & 1);

  return bmc.raw;
}

uint64_t spdifBuildSubframe(spdif_subframe_t* subframe, spdif_preamble_t preamble, int16_t sample)
{
  subframe->sample = sample << 4; // Scale to 20 bits
  subframe->validity = 0; // 0 Indicates OK. Dumb
  subframe->aux = 0;
  subframe->parity = 0; // Reset partiy before calculating
  subframe->parity = __builtin_popcount(subframe->raw) % 2;

  // Encode to biphae mark
  return spdifEncodeBiphaseMark(preamble, __rbit(subframe->raw));
}