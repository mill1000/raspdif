#include <unistd.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <arm_acle.h>
#include <fcntl.h>
#include <queue>

#include "log.h"
#include "bcm283x.h"

#define TAG "MAIN"

// Inverted if preceding state was 1
// Which shouldn't happen due to even parity
#define PREAMBLE_M 0xE2 // Sub-frame 1
#define PREAMBLE_W 0xE4 // Sub-frame 2
#define PREAMBLE_B 0xE8 // Sub-frame 1, start of block

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
  spdif_frame_t frames[192];
} spdif_block_t;


 uint64_t encodeBiphaseMark(spdif_preamble_t preamble, uint32_t data)
{
  //LOGI(TAG, "BMC Input %08X", data);
  
  // BMC LUT for nibbles
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
  // First subframe
  // Ignored    | Aux       | Sample                              | Valid | User | Status | Parity
  // 0000       | 0000      | 0000 0000 0000 0000 0000            | 1     | 0    | 0      | 1
  // 11101000   | 1100 1100 | 11001100 11001100 11001100 11001100 | 10    | 11   | 00     | 10

  // Set preamble bits
  switch (preamble)
  {
    case spdif_preamble_b:
      bmc.byte[7] = PREAMBLE_B;
    break;

    case spdif_preamble_m:
      bmc.byte[7] = PREAMBLE_M;
    break;

    case spdif_preamble_w:
      bmc.byte[7] = PREAMBLE_W;
    break;
  }
  
  // Encode data a nibble at a time
  // Code is inversted if previous state was 1
  // Aux Data
  bmc.byte[6] = bmcLut0[(data >> 24) & 0xF]; // No need to check last state, preamble guaentees 0
  // Sample
  bmc.byte[5] = bmcLut0[(data >> 20) & 0xF] ^ -(int)(bmc.byte[6] & 1);
  bmc.byte[4] = bmcLut0[(data >> 16) & 0xF] ^ -(int)(bmc.byte[5] & 1);
  bmc.byte[3] = bmcLut0[(data >> 12) & 0xF] ^ -(int)(bmc.byte[4] & 1);
  bmc.byte[2] = bmcLut0[(data >> 8) & 0xF] ^  -(int)(bmc.byte[3] & 1);
  bmc.byte[1] = bmcLut0[(data >> 4) & 0xF] ^  -(int)(bmc.byte[2] & 1);
  // Valid, User, Status, Parity
  bmc.byte[0] = bmcLut0[(data >> 0) & 0xF] ^  -(int)(bmc.byte[1] & 1);


  //LOGD(TAG, "BMC Word %016llX", bmc.raw);
  //for (uint8_t i = 0; i < 8; i++)
  //{
  //  LOGI(TAG, "BMC Byte %d: %02X", i, bmc.byte[i]);
  //}

  return bmc.raw;
   // Biphase Marck
  // Each bit to be transmitted is 2 binary states
  // 1st is always different from previous, 2nd is identical if 0, different if 1
  // Assuming previous was 0
  // 00 00 -> 1100 1100    0010 0000 -> 11 00 10 11    00 11 00 11
  // 00 01 -> 1100 1101
  // 00 10 -> 1100 1011
  // 00 11 -> 1100 1010

  // 01 00 -> 1101 0011
  // 01 01 -> 1101 0010
  // 01 10 -> 1101 0100
  // 01 11 -> 1101 0101

  // 10 00 -> 1011 0011
  // 10 01 -> 1011 0010
  // 10 10 -> 1011 0100
  // 10 11 -> 1011 0101

  // 11 00 -> 1010 1100
  // 11 01 -> 1010 1101
  // 11 10 -> 1010 1011
  // 11 11 -> 1010 1010

}

/**
  @brief  Callback function for POSIX signals

  @param  signal Received POSIX signal
  @retval none
*/
static void signalHandler(int32_t signal)
{
  LOGW(TAG, "Received signal %s (%d).", sys_siglist[signal], signal);

  pcmReset();

  // Termiante
  exit(EXIT_SUCCESS);
}

/**
  @brief  Reigster a handler for all POSIX signals 
          that would cause termination

  @param  none
  @retval none
*/
void registerSignalHandler()
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));

  sa.sa_handler = &signalHandler;

  // Register fatal signal handlers
  sigaction(SIGHUP, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
  sigaction(SIGILL, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
  sigaction(SIGFPE, &sa, NULL);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGPIPE, &sa, NULL);
  sigaction(SIGALRM, &sa, NULL);
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
}

/**
  @brief  Main entry point

  @param  argc
  @param  argv
  @retval none
*/
int main (int argc, char* argv[])
{
  // Register signal handlers
  registerSignalHandler();

  // Increase logging level to debug
  logSetLevel(LOG_LEVEL_DEBUG);

  // Initialize BCM peripheral drivers
  bcm283x_init();

  // Configure selected pins as AF0
  gpio_configuration_t gpioConfig;
  gpioConfig.eventDetect = gpio_event_detect_none;
  gpioConfig.function = gpio_function_af0;
  gpioConfig.pull = gpio_pull_no_change;
  gpioConfigureMask(1 << 21 | 1 << 19 , &gpioConfig);
  
  // Configure selected pins as AF0
  
  gpioConfig.eventDetect = gpio_event_detect_none;
  gpioConfig.function = gpio_function_output;
  gpioConfig.pull = gpio_pull_no_change;
  gpioConfigureMask(1 << 23 | 1 << 18 | 1 << 15 | 1 << 14, &gpioConfig);
  

  // Target clock of 5.6448 MHz
  // 44.1 kHz * 64 bits * 2x (Manchester)
  // 500 MHz / 5.6448 = 88.57709750566893

  double divisor = (500.0 / 5.6448);
  //double divisor = 500.0 / 6.0;

  double divi = 0;
  double divf = round(4096 * modf(divisor, &divi));
  LOGI(TAG, "Floating DIVF: %f.", divf);
  LOGI(TAG, "Calculated DIVI: %d, DIVF: %d.", (uint16_t) divi, (uint16_t)divf);
  
  clock_configuration_t clockConfig;
  clockConfig.source = clock_source_plld; // 500 MHz
  clockConfig.mash = clock_mash_filter_1_stage;
  clockConfig.invert = false;
  clockConfig.divi = divi;
  clockConfig.divf = divf;

  clockConfigure(clock_peripheral_pcm, &clockConfig);
  
  clockEnable(clock_peripheral_pcm, true);

  pcmReset();
  LOGI(TAG, "After reset");
  pcmDump();

  pcm_configuration_t pcmConfig;
  pcmConfig.frameSyncLength = 1; // Don't want FS pulse
  pcmConfig.frameSyncInvert = false;
  pcmConfig.frameSyncMode = pcm_frame_sync_master;
  
  pcmConfig.clockInvert = false;
  pcmConfig.clockMode = pcm_clock_master;

  pcmConfig.txFrameMode = pcm_frame_unpacked;
  pcmConfig.rxFrameMode = pcm_frame_unpacked;

  pcmConfig.frameLength = 32;

  pcmConfigure(&pcmConfig);
  LOGI(TAG, "After config");
  pcmDump();

  pcmClearFifos();

  pcm_channel_config_t txConfig[2] = {0};
  txConfig[0].width = 32;
  txConfig[0].position = 0;
  txConfig[0].enable = true;

  pcmConfigureTx(txConfig);
  LOGI(TAG, "After tx config");
  pcmDump();

  spdif_pcm_channel_status_t channel_status_a = {0};
  channel_status_a.aes3 = 0; // SPDIF
  channel_status_a.compressed = 0; // PCM
  channel_status_a.copy_permit = 1; // No copy protection
  channel_status_a.pcm_mode = 0; // 2 channel, no pre-emphasis
  channel_status_a.mode = 0;

  channel_status_a.word_length = 1;

  channel_status_a.channel_number = 1;

  spdif_pcm_channel_status_t channel_status_b = channel_status_a;
  channel_status_b.channel_number = 2;
  

  spdif_block_t block = {0};

  for (uint8_t i = 0; i < 192; i++)
  {
  //  block.frames[i].a.channel_status = channel_status_a.raw[i / 8] >> (i % 8);
  //  block.frames[i].b.channel_status = channel_status_b.raw[i / 8] >> (i % 8);
  }

  
  
  gpioClear(15);
  gpioClear(18);
  gpioClear(14);
  gpioClear(23);


  freopen(NULL, "rb", stdin);

  uint8_t frame_index = 0;
  uint32_t sample_count = 0;

  int last_size = 0;
  std::queue<uint16_t> sample_buffer;

  int16_t samples[2] = {0};
  LOGI(TAG, "Waiting for data...");
  if (fread(samples, sizeof(int16_t), 2, stdin) > 0)
  {
    sample_buffer.push(samples[0]);
    sample_buffer.push(samples[1]);
    if (sample_buffer.size() != last_size)
    {
      LOGI(TAG, "Buffer Depth: %d", sample_buffer.size());
      last_size = sample_buffer.size();
    }
  }
  
  fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);


  pcmEnable();
  while(!feof(stdin) || sample_buffer.size() > 0)
  {
    if (sample_buffer.size() < 16)
    {
      gpioSet(15);
      if (fread(samples, sizeof(int16_t), 2, stdin) > 0)
      {
        sample_buffer.push(samples[0]);
        sample_buffer.push(samples[1]);
        //if (sample_buffer.size() != last_size)
        //{
        //  LOGI(TAG, "Buffer Depth: %d", sample_buffer.size());
        //  last_size = sample_buffer.size();
        //}
      }
      gpioClear(15);
    }
    

    if (!pcmFifoNeedsWriting())
    {
      //LOGW(TAG, "FIFO Full");
      gpioSet(14);
      continue;
    }
    gpioClear(14);

    while (sample_buffer.size() >= 2 && !pcmFifoFull())
    {
      int16_t sampleA = sample_buffer.front();
      sample_buffer.pop();

      int16_t sampleB = sample_buffer.front();
      sample_buffer.pop();
      //LOGI(TAG, "Sample %d - L: %d R: %d", sample_count, sampleA, sampleB);
      
      
      spdif_frame_t* frame = &block.frames[frame_index];

      frame->a.sample = sampleA << 4;
      frame->a.validity = 0; // 0 Indicates OK. Dumb
      frame->a.aux = 0;
      frame->a.parity = 0; // Reset partiy before calculating
      frame->a.parity = __builtin_popcount(frame->a.raw) % 2;

      frame->b.sample = sampleB << 4;
      frame->b.validity = 0;
      frame->b.aux = 0;
      frame->b.parity = 0;
      frame->b.parity = __builtin_popcount(frame->b.raw) % 2;

      //LOGI(TAG, "Frame %d - A: %08X B: %08X", frame_index, frame->a, frame->b);
      gpioSet(18);
      uint64_t codeA = encodeBiphaseMark(frame_index == 0 ? spdif_preamble_b : spdif_preamble_m, __rbit(frame->a.raw));
      pcmWrite(codeA >> 32);
      pcmWrite(codeA);
            gpioClear(18);
      
      gpioSet(23);
      uint64_t codeB = encodeBiphaseMark(spdif_preamble_w, __rbit(frame->b.raw));
      pcmWrite(codeB >> 32);
      pcmWrite(codeB);
      gpioClear(23);

      frame_index = (frame_index + 1) % 192;
      sample_count++;
    }

    
      //if (sample_count > 192)
//        break;




    //if (sample_count == 8)
    //{
    //  LOGI(TAG, "ENABLING");
    //  pcmEnable();
    //}

    //if (sample_count > 200)
    //  break;
  }


  bcm283x_delay_microseconds(100);
  LOGI(TAG, "After enable");
  pcmDump();


  return 0;
}
