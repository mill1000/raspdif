#include <unistd.h>
#include <float.h>
#include <math.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <bcm_host.h>

#include "log.h"
#include "bcm283x.h"
#include "memory.h"
#include "utils.h"
#include "spdif.h"

#define TAG "MAIN"

#define RASPDIF_SAMPLE_RATE  44.1e3 // 44.1 kHz
#define RASPDIF_BUFFER_COUNT 3      // Number of entries in the cirular buffer
#define RASPDIF_BUFFER_SIZE  2048   // Number of bytes in each buffer entry
static_assert(RASPDIF_BUFFER_SIZE <=  UINT16_MAX, "SPDIF buffer must be representable in 16 bits.");

static struct
{
  memory_physical_t memory;
  dma_channel_t     dmaChannel;
} raspdif;

typedef struct raspdif_buffer_t
{
  struct
  {
    uint32_t a_msb;
    uint32_t a_lsb;
    uint32_t b_msb;
    uint32_t b_lsb;
  } sample[RASPDIF_BUFFER_SIZE];
} raspdif_buffer_t;

typedef struct raspdif_control_t
{
  dma_control_block_t controlBlocks[RASPDIF_BUFFER_COUNT];
  raspdif_buffer_t    buffers[RASPDIF_BUFFER_COUNT];
} raspdif_control_t;

/**
  @brief  Shutdown the peripherals and free any allocated memory

  @param  none
  @retval none
*/
void raspdifShutdown()
{
  // Disable PCM and DMA
  pcmReset();
  clockEnable(clock_peripheral_pcm, false);
  dmaEnable(raspdif.dmaChannel, false);
  
  // Free allocated memory
  memoryReleasePhysical(&raspdif.memory);
}

/**
  @brief  Callback function for POSIX signals

  @param  signal Received POSIX signal
  @retval none
*/
static void signalHandler(int32_t signal)
{
  LOGW(TAG, "Received signal %s (%d).", sys_siglist[signal], signal);

  raspdifShutdown();

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
  @brief  Generate the DMA controls blocks for the code buffers

  @param  bControl raspdif_control_t structure in bus domain
  @param  vControl raspdif_control_t strucutre in virtual domain
  @retval none
*/
static void generateDmaControlBlocks(raspdif_control_t* bControl, raspdif_control_t* vControl)
{
  // Construct references to PCM peripheral at its bus addresses
  bcm283x_pcm_t* bPcm = (bcm283x_pcm_t*) (BCM283X_BUS_PERIPHERAL_BASE + PCM_BASE_OFFSET);

  // Zero-init all control blocks  
  memset((void*)vControl->controlBlocks, 0, RASPDIF_BUFFER_COUNT * sizeof(dma_control_block_t));

  for (size_t i = 0; i < RASPDIF_BUFFER_COUNT; i++)
  {
    // Configure DMA control block for this buffer
    dma_control_block_t* control = &vControl->controlBlocks[i];

    control->transferInformation.NO_WIDE_BURSTS = 1;
    control->transferInformation.PERMAP = DMA_DREQ_PCM_TX;
    control->transferInformation.DEST_DREQ = 1;
    control->transferInformation.WAIT_RESP = 1;
    control->transferInformation.SRC_INC = 1;

    control->sourceAddress = &bControl->buffers[i];
    control->destinationAddress = (void*) &bPcm->FIFO_A;
    control->transferLength.XLENGTH = sizeof(raspdif_buffer_t);

    // Point to next block, or first if at end
    control->nextControlBlock = &bControl->controlBlocks[(i+1) % RASPDIF_BUFFER_COUNT];
  }

  // Check that blocks loop
  assert(vControl->controlBlocks[RASPDIF_BUFFER_COUNT - 1].nextControlBlock == &bControl->controlBlocks[0]);
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
  
  // DMA channel 13 seems free...
  raspdif.dmaChannel = dma_channel_13;

  // Allocate buffers and control blocks in physical memory
  memory_physical_t memory = memoryAllocatePhysical(sizeof(raspdif_control_t));
  if (memory.address == NULL)
    LOGF(TAG, "Failed to allocate physical memory.");

  // Save reference to physical memory handle
  raspdif.memory = memory;

  // Map the physical memory into our address space
  uint8_t* busBase = (uint8_t*)memory.address;
  uint8_t* physicalBase = busBase - bcm_host_get_sdram_address();
  uint8_t* virtualBase = (uint8_t*)memoryMapPhysical((off_t)physicalBase, sizeof(raspdif_control_t));
  if (virtualBase == NULL)
  {
    // Free physical memory
    memoryReleasePhysical(&memory);

    LOGF(TAG, "Failed to map physical memory.");
  }

  // Control blocks reference PCM and eachother via bus addresses
  // Application accesses blocks via virtual addresses.
  // Shortcuts to control structures in both domains
  raspdif_control_t* bControl = (raspdif_control_t*) busBase;
  raspdif_control_t* vControl = (raspdif_control_t*) virtualBase;

  // Generate DMA control blocks for each SPDIF buffer
  generateDmaControlBlocks(bControl, vControl);

  // Configure DMA channel to load PCM from the buffers
  dmaReset(raspdif.dmaChannel);
  dmaSetControlBlock(raspdif.dmaChannel, bControl->controlBlocks);

  // Configure GPIO 21 as PCM DOUT via AF0
  gpio_configuration_t gpioConfig;
  gpioConfig.eventDetect = gpio_event_detect_none;
  gpioConfig.function = gpio_function_af0;
  gpioConfig.pull = gpio_pull_no_change;
  gpioConfigureMask(1 << 21 , &gpioConfig);

  // Target clock of 5.6448 MHz
  // 44.1 kHz * 64 bits * 2x (Manchester)
  // 500 MHz / 5.6448 = 88.57709750566893
  double spdif_clock = RASPDIF_SAMPLE_RATE * 64.0 * 2.0;
  LOGD(TAG, "Calculated SPDIF clock of %g Hz for sample rate of %g Hz.", spdif_clock, RASPDIF_SAMPLE_RATE);

  double divisor = (500e6 / spdif_clock);

  double divi = 0;
  double divf = round(4096 * modf(divisor, &divi));
  LOGD(TAG, "Calculated DIVI: %d, DIVF: %d.", (uint16_t) divi, (uint16_t)divf);
  
  clock_configuration_t clockConfig;
  clockConfig.source = clock_source_plld; // 500 MHz
  clockConfig.mash = clock_mash_filter_1_stage; // MASH filters requried for non-integer division
  clockConfig.invert = false;
  clockConfig.divi = divi;
  clockConfig.divf = divf;

  clockConfigure(clock_peripheral_pcm, &clockConfig);
  clockEnable(clock_peripheral_pcm, true);

  // Reset PCM peripheral
  pcmReset();

  // Comnfigure PCM peripheral
  pcm_configuration_t pcmConfig;
  pcmConfig.frameSyncLength = 1; // FS is unsed in SPDIF but useful for debugging
  pcmConfig.frameSyncInvert = false;
  pcmConfig.frameSyncMode = pcm_frame_sync_master;
  
  pcmConfig.clockInvert = false;
  pcmConfig.clockMode = pcm_clock_master;

  pcmConfig.txFrameMode = pcm_frame_unpacked;
  pcmConfig.rxFrameMode = pcm_frame_unpacked;

  pcmConfig.frameLength = 32; // PCM peripheral will transmit 32 bit chunks
  pcmConfigure(&pcmConfig);

  // Configure the transmit channels
  pcm_channel_config_t txConfig[2] = {0};
  txConfig[0].width = 32;
  txConfig[0].position = 0;
  txConfig[0].enable = true;
  pcmConfigureTx(txConfig);
  
  // Clear FIFOs just in case
  pcmClearFifos();

  // Define the SPDIF channel status data
  spdif_pcm_channel_status_t channel_status_a;
  memset(&channel_status_a, 0, sizeof(spdif_pcm_channel_status_t));

  channel_status_a.aes3 = 0; // SPDIF
  channel_status_a.compressed = 0; // PCM
  channel_status_a.copy_permit = 1; // No copy protection
  channel_status_a.pcm_mode = 0; // 2 channel, no pre-emphasis
  channel_status_a.mode = 0;

  channel_status_a.word_length = 1;

  channel_status_a.channel_number = 1;

  // Duplicate channel status for B and update channel number
  spdif_pcm_channel_status_t channel_status_b = channel_status_a;
  channel_status_b.channel_number = 2;

  // Allocate storage for a SPDIF block
  spdif_block_t block;
  memset(&block, 0, sizeof(block));

  // Copy channel status into each frame
  for (uint8_t i = 0; i < SPDIF_FRAME_COUNT; i++)
  {
    block.frames[i].a.channel_status = channel_status_a.raw[i / 8] >> (i % 8);
    block.frames[i].b.channel_status = channel_status_b.raw[i / 8] >> (i % 8);
  }

  // Re-open stdin as binary
  freopen(NULL, "rb", stdin);

  uint8_t frame_index = 0;
  uint32_t sample_count = 0;
  uint8_t buffer_index = 0;
  
  LOGI(TAG, "Waiting for data...");

  // Pre-load the buffers
  int16_t samples[2] = {0};
  while (fread(samples, sizeof(int16_t), 2, stdin) > 0 && buffer_index < RASPDIF_BUFFER_COUNT)
  {
    raspdif_buffer_t* buffer = &vControl->buffers[buffer_index];
    spdif_frame_t* frame = &block.frames[frame_index];

    uint64_t codeA = spdifBuildSubframe(&frame->a, frame_index == 0 ? spdif_preamble_b : spdif_preamble_m, samples[0]);
    buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a_msb = codeA >> 32;
    buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a_lsb = codeA;
    
    uint64_t codeB = spdifBuildSubframe(&frame->b, spdif_preamble_w, samples[1]);
    buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b_msb = codeB >> 32;
    buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b_lsb = codeB;
  
    //LOGI(TAG, "Buffer %d Code %d - A: %016llX B: %016llX", buffer_index, sample_count, codeA, codeB);

    frame_index = (frame_index + 1) % SPDIF_FRAME_COUNT;
    sample_count++;
    buffer_index = sample_count / RASPDIF_BUFFER_SIZE;
  }

  // Enable DMA and PCM to start transmit
  dmaEnable(raspdif.dmaChannel, true);
  pcmEnable();

  // Read stdin until it's empty
  buffer_index = 0;
  sample_count = 0;
  raspdif_buffer_t* buffer = &vControl->buffers[buffer_index];
  while(!feof(stdin))
  {
    const dma_control_block_t* activeControl = dmaGetControlBlock(raspdif.dmaChannel);

    if (activeControl == &bControl->controlBlocks[buffer_index])
    {
      microsleep(50e3);
      continue;
    }

    if (fread(samples, sizeof(int16_t), 2, stdin) > 0)
    {
      spdif_frame_t* frame = &block.frames[frame_index];

      uint64_t codeA = spdifBuildSubframe(&frame->a, frame_index == 0 ? spdif_preamble_b : spdif_preamble_m, samples[0]);
      buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a_msb = codeA >> 32;
      buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a_lsb = codeA;
      
      uint64_t codeB = spdifBuildSubframe(&frame->b, spdif_preamble_w, samples[1]);
      buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b_msb = codeB >> 32;
      buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b_lsb = codeB;

      frame_index = (frame_index + 1) % SPDIF_FRAME_COUNT;
      sample_count++;

      if (sample_count % RASPDIF_BUFFER_SIZE == 0)
      {
        buffer_index = (buffer_index + 1) % RASPDIF_BUFFER_COUNT;
        buffer = &vControl->buffers[buffer_index];
      }
    }
  }

  // Shutdown in a safe mamner
  raspdifShutdown();

  return 0;
}
