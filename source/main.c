#include <argp.h>
#include <bcm_host.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm283x.h"
#include "git_version.h"
#include "log.h"
#include "memory.h"
#include "raspdif.h"
#include "spdif.h"
#include "utils.h"

#define TAG "MAIN"

static struct
{
  memory_physical_t memory;
  dma_channel_t     dmaChannel;
  struct
  {
    raspdif_control_t* bus;
    raspdif_control_t* virtual;
  } control;
} raspdif;

typedef struct raspdif_arguments_t 
{
  const char* file;
  bool    verbose;
  double  sample_rate;
  raspdif_format_t format;
} raspdif_arguments_t;

const char* argp_program_version = "raspdif " GIT_VERSION;
const char* argp_program_bug_address = "https://github.com/mill1000/raspdif/issues";
static struct argp_option options[] =
{
  {"input", 'i', "INPUT_FILE", 0 , "Read data from file instead of stdin."},
  {"rate", 'r', "RATE", 0, "Set audio sample rate. Default: 44.1 kHz"},
  {"format", 'f', "FORMAT", 0, "Set audio sample format to s16le or s24le. Default: s16le"},
  {"verbose", 'v', 0, 0, "Enable debug messages."},
  {0}
};

/**
  @brief  Argument parser for argp

  @param  key Short argument k
  @param  arg String argument to k
  @param  state argp state variable
  @retval error_t
*/
static error_t parse_opt(int key, char* arg, struct argp_state* state)
{
  raspdif_arguments_t* arguments = state->input;

  switch (key)
  {
    case 'f': 
      if (strcmp("s16le", arg) == 0)
        arguments->format = raspdif_format_s16le;
      else if (strcmp("s24le", arg) == 0)
        arguments->format = raspdif_format_s24le;
      else
      {
        LOGF(TAG, "Unrecognized format '%s'", arg);
        return EINVAL;
      }
      
      break;
    case 'i': arguments->file = arg; break;
    case 'r': arguments->sample_rate = strtod(arg, NULL); break;
    case 'v': arguments->verbose = true; break;
    default: return ARGP_ERR_UNKNOWN;
  }
  
  return 0;
}

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
  @brief  Generate the DMA controls blocks for the code buffers

  @param  bControl raspdif_control_t structure in bus domain
  @param  vControl raspdif_control_t structure in virtual domain
  @retval none
*/
static void raspdifGenerateDmaControlBlocks(raspdif_control_t* bControl, raspdif_control_t* vControl)
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
  @brief  Initialize hardware for SPDIF generation. Include DMA, Clock, PCM and GPIO config

  @param  dmaChannel DMA channel to use for transfering buffers to PCM
  @param  sampleRate_Hz Audio sample rate in Hertz for clock configuration
  @retval none
*/
static void raspdifInit(dma_channel_t dmaChannel, double sampleRate_Hz)
{
  // Initialize BCM peripheral drivers
  bcm283x_init();
  
  // Save DMA channel
  raspdif.dmaChannel = dmaChannel;

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

  // Control blocks reference PCM and each other via bus addresses
  // Application accesses blocks via virtual addresses.
  // Shortcuts to control structures in both domains
  raspdif_control_t* bControl = (raspdif_control_t*) busBase;
  raspdif_control_t* vControl = (raspdif_control_t*) virtualBase;

  // Generate DMA control blocks for each SPDIF buffer
  raspdifGenerateDmaControlBlocks(bControl, vControl);

  // Save references to control structures in both domains
  raspdif.control.bus = bControl;
  raspdif.control.virtual = vControl;

  // Configure DMA channel to load PCM from the buffers
  dmaReset(raspdif.dmaChannel);
  dmaSetControlBlock(raspdif.dmaChannel, bControl->controlBlocks);

  // Calculate required PCM clock rate for sample rate
  // 44.1 kHz * 64 bits * 2x (Manchester) -> 5.6448 MHz
  // 500 MHz / 5.6448 = 88.57709750566893
  double spdif_clock = sampleRate_Hz * 64.0 * 2.0;
  LOGD(TAG, "Calculated SPDIF clock of %g Hz for sample rate of %g Hz.", spdif_clock, sampleRate_Hz);
  
  double divisor = (500e6 / spdif_clock);

  double divi = 0;
  double divf = round(4096 * modf(divisor, &divi));
  LOGD(TAG, "Calculated DIVI: %d, DIVF: %d.", (uint16_t) divi, (uint16_t)divf);
  
  clock_configuration_t clockConfig;
  clockConfig.source = clock_source_plld; // 500 MHz
  clockConfig.mash = clock_mash_filter_1_stage; // MASH filters required for non-integer division
  clockConfig.invert = false;
  clockConfig.divi = divi;
  clockConfig.divf = divf;

  clockConfigure(clock_peripheral_pcm, &clockConfig);
  clockEnable(clock_peripheral_pcm, true);

  // Reset PCM peripheral
  pcmReset();

  // Configure PCM frame, clock and sync modes
  pcm_configuration_t pcmConfig;
  memset(&pcmConfig, 0, sizeof(pcm_configuration_t));

  pcmConfig.frameSync.length = 1; // FS is unused in SPDIF but useful for debugging
  pcmConfig.frameSync.invert = false;
  pcmConfig.frameSync.mode = pcm_frame_sync_master;
  
  pcmConfig.clock.invert = false;
  pcmConfig.clock.mode = pcm_clock_master;

  pcmConfig.frame.txMode = pcm_frame_unpacked;
  pcmConfig.frame.rxMode = pcm_frame_unpacked;

  pcmConfig.frame.length = 32; // PCM peripheral will transmit 32 bit chunks
  pcmConfigure(&pcmConfig);

  // Enable PCM DMA request and FIFO thresholds
  pcm_dma_config_t dmaConfig;
  memset(&dmaConfig, 0, sizeof(pcm_dma_config_t));

  dmaConfig.txThreshold = 32;
  dmaConfig.txPanic = 16;
  pcmConfigureDma(true, &dmaConfig);

  // Configure the transmit channel 1 for 32 bits
  pcm_channel_config_t txConfig;
  txConfig.width = 32;
  txConfig.position = 0;
  pcmConfigureTransmitChannels(&txConfig, NULL);
  
  // Clear FIFOs just in case
  pcmClearFifos();

  // Configure GPIO 21 as PCM DOUT via AF0
  gpio_configuration_t gpioConfig;
  gpioConfig.eventDetect = gpio_event_detect_none;
  gpioConfig.function = gpio_function_af0;
  gpioConfig.pull = gpio_pull_no_change;
  gpioConfigureMask(1 << 21 , &gpioConfig);
}

/**
  @brief  Encode and store the audio samples into the target buffer

  @param  buffer Buffer to store encoded samples to
  @param  block SPDIF block so proper frames can be encoded
  @param  format Format of samples
  @param  sampleA Audio sample for first channel
  @param  sampleB Audio sample for second channel
  @retval bool - Provided buffer is now full
*/
static bool raspdifBufferSamples(raspdif_buffer_t* buffer, spdif_block_t* block, raspdif_format_t format, int32_t sampleA, int32_t sampleB)
{
  static uint8_t frame_index = 0; // Position within SPDIF block
  static uint32_t sample_count = 0; // Number of samples received. At 44.1 kHz will overflow at 13 hours

  spdif_sample_depth_t bitDepth = (format == raspdif_format_s24le) ? spdif_sample_depth_24 : spdif_sample_depth_16;

  spdif_frame_t* frame = &block->frames[frame_index];

  uint64_t codeA = spdifBuildSubframe(&frame->a, frame_index == 0 ? spdif_preamble_b : spdif_preamble_m, bitDepth, sampleA);
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a.msb = codeA >> 32;
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a.lsb = codeA;
  
  uint64_t codeB = spdifBuildSubframe(&frame->b, spdif_preamble_w, bitDepth, sampleB);
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b.msb = codeB >> 32;
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b.lsb = codeB;

  frame_index = (frame_index + 1) % SPDIF_FRAME_COUNT;
  sample_count++;

  return sample_count % RASPDIF_BUFFER_SIZE == 0;
}

/**
  @brief  Parse and sign extend the sample of the specified format

  @param  format Sample format to parse
  @param  buffer Buffer containing raw sample bytes
  @retval int32_t - Sign extended sample
*/
static int32_t raspdifParseSample( raspdif_format_t format, uint8_t* buffer)
{
  if (format == raspdif_format_s16le)
  {
    return (int16_t) (buffer[1] << 8 | buffer[0]);
  }
  else
  {
    struct { signed int x : 24; } s;
    return s.x = buffer[2] << 16 | buffer[1] << 8 | buffer[0];
  }
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

  // Terminate
  exit(EXIT_SUCCESS);
}

/**
  @brief  Register a handler for all POSIX signals 
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
int main(int argc, char* argv[])
{
  raspdif_arguments_t arguments;
  memset(&arguments, 0, sizeof(raspdif_arguments_t));

  // Set default sample rate
  arguments.sample_rate = RASPDIF_DEFAULT_SAMPLE_RATE;
  arguments.format = RASPDIF_DEFAULT_FORMAT;

  // Parse command line args
  struct argp argp = {options, parse_opt, NULL, NULL};
  argp_parse (&argp, argc, argv, 0, 0, &arguments);

  // Register signal handlers
  registerSignalHandler();

  // Increase logging level to debug if requested
  if (arguments.verbose)
    logSetLevel(LOG_LEVEL_DEBUG);

  // Initialize hardware and buffers
  raspdifInit(dma_channel_13, arguments.sample_rate);

  // Allocate storage for a SPDIF block
  spdif_block_t block;
  memset(&block, 0, sizeof(block));

  // Populate each frame with channel status data
  spdifPopulateChannelStatus(&block);
  
  // Open the target file or stdin
  FILE* file = NULL;
  if (arguments.file)
    file = fopen(arguments.file, "r+b"); // Open with writing to prevent EOF when FIFO is empty
  else
    file = freopen(NULL, "rb", stdin);
  
  LOGI(TAG, "Estimated latency: %g seconds.", (RASPDIF_BUFFER_COUNT - 1) * (RASPDIF_BUFFER_SIZE / arguments.sample_rate));
  LOGI(TAG, "Waiting for data...");
  
  // Determine sample size in bytes
  uint8_t sampleSize = (arguments.format == raspdif_format_s24le) ? 3 : sizeof(int16_t);

  // Pre-load the buffers
  uint8_t buffer_index = 0;
  uint8_t samples[2 * sizeof(int32_t)] = {0};
  while (fread(samples, sampleSize, 2, file) > 0 && buffer_index < RASPDIF_BUFFER_COUNT)
  {
    raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];

    // Parse sample buffer in proper format
    int32_t sampleA = raspdifParseSample(arguments.format, &samples[0]);
    int32_t sampleB = raspdifParseSample(arguments.format, &samples[sampleSize]);

    bool full = raspdifBufferSamples(buffer, &block, arguments.format, sampleA, sampleB);
    
      if (full)
        buffer_index++;
  }
  
  LOGI(TAG, "Transmitting...");

  // Enable DMA and PCM to start transmit
  dmaEnable(raspdif.dmaChannel, true);
  pcmEnable(true, false);

  // Set stndin to nonblocking
  fcntl(fileno(file), F_SETFL, O_NONBLOCK);

  // Reset to first buffer.
  buffer_index = 0;
  
  // Read file until EOS. Note: files opened in r+ will not emit EOF
  while(!feof(file))
  {
    const dma_control_block_t* activeControl = dmaGetControlBlock(raspdif.dmaChannel);

    if (activeControl == &raspdif.control.bus->controlBlocks[buffer_index])
    {
      // If DMA is using current buffer, delay by approx 1 buffer's duration
      microsleep(1e6 * (RASPDIF_BUFFER_SIZE / arguments.sample_rate));
      continue;
    }

    // If read fails (or would block) pause the stream
    if (fread(samples, sampleSize, 2, file) != 2 && !feof(file))
    {
      LOGD(TAG, "Buffer underrun. Paused transmit.");

      // Disable PCM transmit 
      pcmEnable(false, false);

      // Wait for file to be readable
      struct pollfd poll_list;
      poll_list.fd = fileno(file);
      poll_list.events = POLLIN;
      poll(&poll_list, 1, -1);

      LOGD(TAG, "Data available. Resume transmit.");

      // Re-enable TX and resume read loop
      pcmEnable(true, false);

      continue;
    }

    raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];

    // Parse sample buffer in proper format
    int32_t sampleA = raspdifParseSample(arguments.format, &samples[0]);
    int32_t sampleB = raspdifParseSample(arguments.format, &samples[sampleSize]);

    bool full = raspdifBufferSamples(buffer, &block, arguments.format, sampleA, sampleB);

    if (full)
      buffer_index = (buffer_index + 1) % RASPDIF_BUFFER_COUNT;
  }

  // TODO How do we wait until the end of the stream

  // Shutdown in a safe manner
  raspdifShutdown();

  return 0;
}
