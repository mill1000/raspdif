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
  dma_channel_t dma_channel;
  struct
  {
    raspdif_control_t* bus;
    raspdif_control_t* virtual;
  } control;
} raspdif;

typedef struct raspdif_arguments_t
{
  const char* file;
  bool verbose;
  bool keep_alive;
  bool pcm_disable;
  double sample_rate;
  raspdif_format_t format;
} raspdif_arguments_t;

const char* argp_program_version = "raspdif " GIT_VERSION;
const char* argp_program_bug_address = "https://github.com/mill1000/raspdif/issues";
static struct argp_option options[] = {
  {"input", 'i', "INPUT_FILE", 0, "Read data from file instead of stdin."},
  {"rate", 'r', "RATE", 0, "Set audio sample rate. Default: 44.1 kHz"},
  {"format", 'f', "FORMAT", 0, "Set audio sample format to s16le or s24le. Default: s16le"},
  {"no-keep-alive", 'k', 0, 0, "Don't send silent noise during underrun."},
  {"disable-pcm-on-idle", 'd', 0, 0, "Disable PCM during underrun."},
  {"verbose", 'v', 0, 0, "Enable debug messages."},
  {0},
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

    case 'i':
      arguments->file = arg;
      break;

    case 'r':
      arguments->sample_rate = strtod(arg, NULL);
      break;

    case 'v':
      arguments->verbose = true;
      break;

    case 'k':
      arguments->keep_alive = false;
      break;

    case 'd':
      arguments->pcm_disable = true;
      break;

    default:
      return ARGP_ERR_UNKNOWN;
  }

  return 0;
}

/**
  @brief  Shutdown the peripherals and free any allocated memory

  @param  none
  @retval none
*/
void raspdif_shutdown()
{
  // Disable PCM and DMA
  bcm283x_pcm_reset();
  bcm283x_clock_enable(clock_peripheral_pcm, false);
  bcm283x_dma_enable(raspdif.dma_channel, false);

  // Free allocated memory
  memory_release_physical(&raspdif.memory);
}

/**
  @brief  Generate the DMA controls blocks for the code buffers

  @param  b_control raspdif_control_t structure in bus domain
  @param  v_control raspdif_control_t structure in virtual domain
  @retval none
*/
static void raspdif_generate_dma_control_blocks(raspdif_control_t* b_control, raspdif_control_t* v_control)
{
  // Construct references to PCM peripheral at its bus addresses
  bcm283x_pcm_t* b_pcm = (bcm283x_pcm_t*)(BCM283X_BUS_PERIPHERAL_BASE + PCM_BASE_OFFSET);

  // Zero-init all control blocks
  memset((void*)v_control->control_blocks, 0, RASPDIF_BUFFER_COUNT * sizeof(dma_control_block_t));

  for (size_t i = 0; i < RASPDIF_BUFFER_COUNT; i++)
  {
    // Configure DMA control block for this buffer
    dma_control_block_t* control = &v_control->control_blocks[i];

    control->transfer_information.NO_WIDE_BURSTS = 1;
    control->transfer_information.PERMAP = DMA_DREQ_PCM_TX;
    control->transfer_information.DEST_DREQ = 1;
    control->transfer_information.WAIT_RESP = 1;
    control->transfer_information.SRC_INC = 1;

    control->source_address = PTR32_CAST(&b_control->buffers[i]);
    control->destination_address = PTR32_CAST(&b_pcm->FIFO_A);
    control->transfer_length.XLENGTH = sizeof(raspdif_buffer_t);

    // Point to next block, or first if at end
    control->next_control_block = PTR32_CAST(&b_control->control_blocks[(i + 1) % RASPDIF_BUFFER_COUNT]);
  }

  // Check that blocks loop
  assert(v_control->control_blocks[RASPDIF_BUFFER_COUNT - 1].next_control_block == PTR32_CAST(&b_control->control_blocks[0]));
}

/**
  @brief  Initialize hardware for SPDIF generation. Include DMA, Clock, PCM and GPIO config

  @param  dma_channel DMA channel to use for transferring buffers to PCM
  @param  sample_rate_hz Audio sample rate in Hertz for clock configuration
  @retval none
*/
static void raspdif_init(dma_channel_t dma_channel, double sample_rate_hz)
{
  // Initialize BCM peripheral drivers
  bcm283x_init();

  // Save DMA channel
  raspdif.dma_channel = dma_channel;
  LOGD(TAG, "Initializing with DMA channel %d.", dma_channel);

  // Allocate buffers and control blocks in physical memory
  memory_physical_t memory = memory_allocate_physical(sizeof(raspdif_control_t));
  if (memory.address == PTR32_NULL)
    LOGF(TAG, "Failed to allocate physical memory.");

  // Save reference to physical memory handle
  raspdif.memory = memory;

  // Map the physical memory into our address space
  uint8_t* bus_base = (uint8_t*)(uintptr_t)memory.address;
  uint8_t* physical_base = bus_base - bcm_host_get_sdram_address();
  uint8_t* virtual_base = (uint8_t*)memory_map_physical((off_t)physical_base, sizeof(raspdif_control_t));
  if (virtual_base == NULL)
  {
    // Free physical memory
    memory_release_physical(&memory);

    LOGF(TAG, "Failed to map physical memory.");
  }

  // Control blocks reference PCM and each other via bus addresses
  // Application accesses blocks via virtual addresses.
  // Shortcuts to control structures in both domains
  raspdif_control_t* b_control = (raspdif_control_t*)bus_base;
  raspdif_control_t* v_control = (raspdif_control_t*)virtual_base;

  // Generate DMA control blocks for each SPDIF buffer
  raspdif_generate_dma_control_blocks(b_control, v_control);

  // Save references to control structures in both domains
  raspdif.control.bus = b_control;
  raspdif.control.virtual = v_control;

  // Configure DMA channel to load PCM from the buffers
  bcm283x_dma_reset(raspdif.dma_channel);
  bcm283x_dma_set_control_block(raspdif.dma_channel, b_control->control_blocks);

  // Calculate required PCM clock rate for sample rate
  // 44.1 kHz * 64 bits * 2x (Manchester) -> 5.6448 MHz
  // 500 MHz / 5.6448 = 88.57709750566893
  double spdif_clock = sample_rate_hz * 64.0 * 2.0;
  LOGD(TAG, "Calculated SPDIF clock of %g Hz for sample rate of %g Hz.", spdif_clock, sample_rate_hz);

  double pll_freq_hz = bcm_host_is_model_pi4() ? 750e6 : 500e6;
  double divisor = (pll_freq_hz / spdif_clock);

  double divi = 0;
  double divf = round(4096 * modf(divisor, &divi));
  LOGD(TAG, "Calculated DIVI: %d, DIVF: %d.", (uint16_t)divi, (uint16_t)divf);

  clock_configuration_t clock_config;
  clock_config.source = clock_source_plld;       // 500 MHz
  clock_config.mash = clock_mash_filter_1_stage; // MASH filters required for non-integer division
  clock_config.invert = false;
  clock_config.divi = divi;
  clock_config.divf = divf;

  bcm283x_clock_configure(clock_peripheral_pcm, &clock_config);
  bcm283x_clock_enable(clock_peripheral_pcm, true);

  // Reset PCM peripheral
  bcm283x_pcm_reset();

  // Configure PCM frame, clock and sync modes
  pcm_configuration_t pcm_config;
  memset(&pcm_config, 0, sizeof(pcm_configuration_t));

  pcm_config.frame_sync.length = 1; // FS is unused in SPDIF but useful for debugging
  pcm_config.frame_sync.invert = false;
  pcm_config.frame_sync.mode = pcm_frame_sync_master;

  pcm_config.clock.invert = false;
  pcm_config.clock.mode = pcm_clock_master;

  pcm_config.frame.tx_mode = pcm_frame_unpacked;
  pcm_config.frame.rx_mode = pcm_frame_unpacked;

  pcm_config.frame.length = 32; // PCM peripheral will transmit 32 bit chunks
  bcm283x_pcm_configure(&pcm_config);

  // Enable PCM DMA request and FIFO thresholds
  pcm_dma_config_t dma_config;
  memset(&dma_config, 0, sizeof(pcm_dma_config_t));

  dma_config.tx_threshold = 32;
  dma_config.tx_panic = 16;
  bcm283x_pcm_configure_dma(true, &dma_config);

  // Configure the transmit channel 1 for 32 bits
  pcm_channel_config_t tx_config;
  tx_config.width = 32;
  tx_config.position = 0;
  bcm283x_pcm_configure_transmit_channels(&tx_config, NULL);

  // Clear FIFOs just in case
  bcm283x_pcm_clear_fifos();

  // Configure GPIO 21 as PCM DOUT via AF0
  gpio_configuration_t gpio_config;
  gpio_config.event_detect = gpio_event_detect_none;
  gpio_config.function = gpio_function_af0;
  gpio_config.pull = gpio_pull_no_change;
  bcm283x_gpio_configure_mask(1 << 21, &gpio_config);
}

/**
  @brief  Encode and store the audio samples into the target buffer

  @param  buffer Buffer to store encoded samples to
  @param  block SPDIF block so proper frames can be encoded
  @param  format Format of samples
  @param  sample_a Audio sample for first channel
  @param  sample_b Audio sample for second channel
  @retval bool - Provided buffer is now full
*/
static bool raspdif_buffer_samples(raspdif_buffer_t* buffer, spdif_block_t* block, raspdif_format_t format, int32_t sample_a, int32_t sample_b)
{
  static uint8_t frame_index = 0;   // Position within SPDIF block
  static uint32_t sample_count = 0; // Number of samples received. At 44.1 kHz will overflow at 13 hours

  spdif_sample_depth_t bit_depth = (format == raspdif_format_s24le) ? spdif_sample_depth_24 : spdif_sample_depth_16;

  spdif_frame_t* frame = &block->frames[frame_index];

  uint64_t code_a = spdif_build_subframe(&frame->a, frame_index == 0 ? spdif_preamble_b : spdif_preamble_m, bit_depth, sample_a);
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a.msb = code_a >> 32;
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].a.lsb = code_a;

  uint64_t code_b = spdif_build_subframe(&frame->b, spdif_preamble_w, bit_depth, sample_b);
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b.msb = code_b >> 32;
  buffer->sample[sample_count % RASPDIF_BUFFER_SIZE].b.lsb = code_b;

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
static int32_t raspdif_parse_sample(raspdif_format_t format, uint8_t* buffer)
{
  if (format == raspdif_format_s16le)
    return (int16_t)(buffer[1] << 8 | buffer[0]);

  // Sign-extend buffer to int32_t
  struct
  {
    signed int x : 24;
  } s;
  return s.x = buffer[2] << 16 | buffer[1] << 8 | buffer[0];
}

/**
  @brief  Fill all buffers with white noise or zeros

  @param  buffer_index Current buffer index to start filling from
  @param  block SPDIF block so proper frames can be encoded
  @param  format Format of samples
  @param  sample_rate Sample rate to estimate latency when delaying on DMA
  @param  keep_alive Transmit quiet white noise to keep equipment alive
  @retval none
*/
static void raspdif_fill_buffers(uint8_t buffer_index, spdif_block_t* block, raspdif_format_t format, double sample_rate, bool keep_alive)
{
  // Seed random generator if using keep-alive
  if (keep_alive)
    srand(time(NULL));

// Macro to generate samples for fill
#define FILL_SAMPLE (keep_alive ? ((rand() % 10) - 5) : 0)

  // Zero fill remainder of current buffer
  raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];
  while (!raspdif_buffer_samples(buffer, block, format, FILL_SAMPLE, FILL_SAMPLE))
  {
  }

  buffer_index = (buffer_index + 1) % RASPDIF_BUFFER_COUNT;

  // Fill all the buffers, while waiting on DMA if necessary
  uint8_t fill_count = 0;
  while (fill_count < RASPDIF_BUFFER_COUNT)
  {
    if (bcm283x_dma_get_control_block(raspdif.dma_channel) == &raspdif.control.bus->control_blocks[buffer_index])
    {
      // If DMA is using current buffer, delay by approx 1 buffer's duration
      microsleep(1e6 * (RASPDIF_BUFFER_SIZE / sample_rate));
      continue;
    }

    raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];
    while (!raspdif_buffer_samples(buffer, block, format, FILL_SAMPLE, FILL_SAMPLE))
    {
    }

    buffer_index = (buffer_index + 1) % RASPDIF_BUFFER_COUNT;

    fill_count++;
  }
}

/**
  @brief  Callback function for POSIX signals

  @param  signal Received POSIX signal
  @retval none
*/
static void signal_handler(int32_t signal)
{
  LOGW(TAG, "Received signal %s (%d).", strsignal(signal), signal);

  raspdif_shutdown();

  // Terminate
  exit(EXIT_SUCCESS);
}

/**
  @brief  Register a handler for all POSIX signals
          that would cause termination

  @param  none
  @retval none
*/
void register_signal_handler()
{
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));

  sa.sa_handler = &signal_handler;

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

  // Set default sample rate, format and keep-alive
  arguments.sample_rate = RASPDIF_DEFAULT_SAMPLE_RATE;
  arguments.format = RASPDIF_DEFAULT_FORMAT;
  arguments.keep_alive = true;

  // Parse command line args
  struct argp argp = {options, parse_opt, NULL, NULL};
  argp_parse(&argp, argc, argv, 0, 0, &arguments);

  // Register signal handlers
  register_signal_handler();

  // Increase logging level to debug if requested
  if (arguments.verbose)
    log_set_level(log_level_debug);

#ifdef TARGET_64BIT
  LOGW(TAG, "64 bit support is experimental. Please report any issues.");
#endif

  // Initialize hardware and buffers
  dma_channel_t dma_channel = bcm_host_is_model_pi4() ? dma_channel_5 : dma_channel_13;
  raspdif_init(dma_channel, arguments.sample_rate);

  // Allocate storage for a SPDIF block
  spdif_block_t block;
  memset(&block, 0, sizeof(block));

  // Populate each frame with channel status data
  spdif_populate_channel_status(&block);

  // Open the target file or stdin
  FILE* file = NULL;
  if (arguments.file)
    file = fopen(arguments.file, "r+b"); // Open with writing to prevent EOF when FIFO is empty
  else
    file = freopen(NULL, "rb", stdin);

  // Failed to open file
  if (file == NULL)
    LOGF(TAG, "Unable to open file. Error: %s.", strerror(errno));

  LOGI(TAG, "Estimated latency: %g seconds.", (RASPDIF_BUFFER_COUNT - 1) * (RASPDIF_BUFFER_SIZE / arguments.sample_rate));
  LOGI(TAG, "Waiting for data...");

  // Determine sample size in bytes
  uint8_t sample_size = (arguments.format == raspdif_format_s24le) ? 3 : sizeof(int16_t);

  // Pre-load the buffers
  uint8_t buffer_index = 0;
  uint8_t samples[2 * sizeof(int32_t)] = {0};
  while (fread(samples, sample_size, 2, file) > 0 && buffer_index < RASPDIF_BUFFER_COUNT)
  {
    // Parse sample buffer in proper format
    int32_t sample_a = raspdif_parse_sample(arguments.format, &samples[0]);
    int32_t sample_b = raspdif_parse_sample(arguments.format, &samples[sample_size]);

    raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];
    bool full = raspdif_buffer_samples(buffer, &block, arguments.format, sample_a, sample_b);

    if (full)
      buffer_index++;
  }

  LOGI(TAG, "Transmitting...");

  // Enable DMA and PCM to start transmit
  bcm283x_dma_enable(raspdif.dma_channel, true);
  bcm283x_pcm_enable(true, false);

  // Set stndin to nonblocking
  fcntl(fileno(file), F_SETFL, O_NONBLOCK);

  // Reset to first buffer.
  buffer_index = 0;

  // Read file until EOS. Note: files opened in r+ will not emit EOF
  while (!feof(file))
  {
    if (bcm283x_dma_get_control_block(raspdif.dma_channel) == &raspdif.control.bus->control_blocks[buffer_index])
    {
      // If DMA is using current buffer, delay by approx 1 buffer's duration
      microsleep(1e6 * (RASPDIF_BUFFER_SIZE / arguments.sample_rate));
      continue;
    }

    // If read fails (or would block) pause the stream
    if (fread(samples, sample_size, 2, file) != 2 && !feof(file))
    {
      LOGD(TAG, "Buffer underrun.");

      // Zero fill the sample buffers for silence
      raspdif_fill_buffers(buffer_index, &block, arguments.format, arguments.sample_rate, arguments.keep_alive);

      if(arguments.pcm_disable)
      {
        bcm283x_pcm_enable(false, false);
        LOGD(TAG, "PCM disabled.");
      }
      
      // Wait for file to be readable
      struct pollfd poll_list;
      poll_list.fd = fileno(file);
      poll_list.events = POLLIN;
      poll(&poll_list, 1, -1);

      if(arguments.pcm_disable)
      {
        bcm283x_pcm_enable(true, false);
        LOGD(TAG, "PCM enabled.");
      }

      // Resume read loop
      LOGD(TAG, "Data available.");
      continue;
    }

    // Parse sample buffer in proper format
    int32_t sample_a = raspdif_parse_sample(arguments.format, &samples[0]);
    int32_t sample_b = raspdif_parse_sample(arguments.format, &samples[sample_size]);

    raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[buffer_index];
    bool full = raspdif_buffer_samples(buffer, &block, arguments.format, sample_a, sample_b);

    if (full)
      buffer_index = (buffer_index + 1) % RASPDIF_BUFFER_COUNT;
  }

  // TODO How do we wait until the end of the stream

  // Shutdown in a safe manner
  raspdif_shutdown();

  return 0;
}
