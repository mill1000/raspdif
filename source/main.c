#define _GNU_SOURCE // F_SETPIPE_SZ
#include <argp.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>

#include "bcm283x.h"
#include "git_version.h"
#include "input.h"
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
  input_t* input; // closed on shutdown, also from the signal handler
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
  int dma_channel; // -1 = model default
  int pipe_size;   // 0 = leave the FIFO at its kernel default (64 KiB)
  uint32_t lead;   // buffers queued ahead of the DMA, 0 = default
  int rt_priority; // SCHED_FIFO priority, 0 = normal scheduler
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
  {"disable-pcm-on-idle", 'd', 0, 0, "Stop the PCM transmitter after one second without input."},
  {"dma-channel", 'c', "CHANNEL", 0, "DMA channel to use. Default: 10 on Pi 4, 13 otherwise"},
  {"pipe-size", 'p', "BYTES", 0, "FIFO input: shrink the pipe to BYTES to cut latency (kernel rounds up to a power-of-two number of pages)."},
  {"lead", 'l', "BUFFERS", 0, "Buffers kept queued ahead of the DMA (1 to buffer count minus 2); sets the output latency."},
  {"rt-priority", 't', "PRIO", 0, "SCHED_FIFO priority, 0 for the normal scheduler. Default: 50"},
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

    case 'c':
    {
      char* end = NULL;
      long channel = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || channel < 0 || channel >= dma_channel_max)
      {
        LOGF(TAG, "Invalid DMA channel '%s'", arg);
        return EINVAL;
      }
      arguments->dma_channel = (int)channel;
      break;
    }

    case 'p':
    {
      char* end = NULL;
      long size = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || size <= 0 || size > (1 << 20))
      {
        LOGF(TAG, "Invalid pipe size '%s'", arg);
        return EINVAL;
      }
      arguments->pipe_size = (int)size;
      break;
    }

    case 'l':
    {
      char* end = NULL;
      long lead = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || lead < 1 || lead > RASPDIF_BUFFER_COUNT - 2)
      {
        LOGF(TAG, "Invalid lead '%s', must be 1..%d", arg, RASPDIF_BUFFER_COUNT - 2);
        return EINVAL;
      }
      arguments->lead = (uint32_t)lead;
      break;
    }

    case 't':
    {
      char* end = NULL;
      long prio = strtol(arg, &end, 10);
      if (end == arg || *end != '\0' || prio < 0 || prio > 99)
      {
        LOGF(TAG, "Invalid real-time priority '%s', must be 0..99", arg);
        return EINVAL;
      }
      arguments->rt_priority = (int)prio;
      break;
    }

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

  // Close the input
  input_close(raspdif.input);
  raspdif.input = NULL;

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
  // Not memset: the blocks live in uncached device memory, and glibc's aarch64
  // memset uses DC ZVA for larger sizes, which faults there (SIGBUS from 1 KiB on).
  volatile uint32_t* words = (volatile uint32_t*)v_control->control_blocks;
  for (size_t w = 0; w < RASPDIF_BUFFER_COUNT * sizeof(dma_control_block_t) / sizeof(uint32_t); w++)
    words[w] = 0;

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
  @brief  Configure and start the PCM clock for a sample rate

  @param  sample_rate_hz Audio sample rate in Hertz
  @retval none
*/
static void raspdif_set_clock(double sample_rate_hz)
{
  // Calculate required PCM clock rate for sample rate
  // 44.1 kHz * 64 bits * 2x (Manchester) -> 5.6448 MHz
  // 500 MHz / 5.6448 = 88.57709750566893
  double spdif_clock = sample_rate_hz * 64.0 * 2.0;
  LOGD(TAG, "Calculated SPDIF clock of %g Hz for sample rate of %g Hz.", spdif_clock, sample_rate_hz);

  double pll_freq_hz = bcm283x_is_model_pi4() ? 750e6 : 500e6;
  double divisor = (pll_freq_hz / spdif_clock);

  double divi = 0;
  double divf = round(4096 * modf(divisor, &divi));
  LOGD(TAG, "Calculated DIVI: %d, DIVF: %d.", (uint16_t)divi, (uint16_t)divf);

  clock_configuration_t clock_config;
  clock_config.source = clock_source_plld;       // 500 MHz (750 MHz on the Pi 4)
  clock_config.mash = clock_mash_filter_1_stage; // MASH filters required for non-integer division
  clock_config.invert = false;
  clock_config.divi = divi;
  clock_config.divf = divf;

  bcm283x_clock_configure(clock_peripheral_pcm, &clock_config);
  bcm283x_clock_enable(clock_peripheral_pcm, true);
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

  // The kernel's DMA engine hands out channels from the same pool
  // (brcm,dma-channel-mask). Never take over a channel that is running.
  if (bcm283x_dma_active(dma_channel))
    LOGF(TAG, "DMA channel %d is active, probably in use by the kernel. Choose another with --dma-channel.", dma_channel);

  // Allocate buffers and control blocks in physical memory
  memory_physical_t memory = memory_allocate_physical(sizeof(raspdif_control_t));
  if (memory.address == PTR32_NULL)
    LOGF(TAG, "Failed to allocate physical memory.");

  // Save reference to physical memory handle
  raspdif.memory = memory;

  // Map the physical memory into our address space
  uint8_t* bus_base = (uint8_t*)(uintptr_t)memory.address;
  uint8_t* physical_base = bus_base - bcm283x_get_sdram_address();
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
  bcm283x_dma_set_priority(raspdif.dma_channel, 8, 15);

  raspdif_set_clock(sample_rate_hz);

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

// IEC 61937 burst preamble: Pa in the first, Pb in the second sub-frame
#define IEC61937_PA 0xF872
#define IEC61937_PB 0x4E1F
// Back to PCM after this many frames without a burst. AC3 repeats every 1536
// frames, DTS every 512..2048; 8192 frames are ~170 ms at 48 kHz.
#define IEC61937_TIMEOUT_FRAMES 8192

static bool raspdif_non_audio = false; // IEC 61937 data is flowing

/**
  @brief  Follow the input and flag IEC 61937 data (AC3/DTS passthrough) as non-audio

          A player in passthrough mode (e.g. Kodi) packs the bitstream into
          16 bit stereo "PCM". The receiver must see channel status bit 1 set,
          otherwise it may play the data as noise. Only 16 bit input can carry it.

  @param  block SPDIF block whose channel status is updated
  @param  format Input sample format
  @param  sample_a First sub-frame sample
  @param  sample_b Second sub-frame sample
  @retval none
*/
static void raspdif_detect_iec61937(spdif_block_t* block, raspdif_format_t format, int32_t sample_a, int32_t sample_b)
{
  static uint32_t frames_since_burst = 0;

  if (format != raspdif_format_s16le)
    return;

  if ((uint16_t)sample_a == IEC61937_PA && (uint16_t)sample_b == IEC61937_PB)
  {
    frames_since_burst = 0;
    if (!raspdif_non_audio)
    {
      raspdif_non_audio = true;
      spdif_set_non_audio(block, true);
      LOGI(TAG, "IEC 61937 data detected, signalling non-audio.");
    }
    return;
  }

  if (raspdif_non_audio && ++frames_since_burst > IEC61937_TIMEOUT_FRAMES)
  {
    raspdif_non_audio = false;
    spdif_set_non_audio(block, false);
    LOGI(TAG, "No IEC 61937 bursts for %d frames, back to PCM.", IEC61937_TIMEOUT_FRAMES);
  }
}

/**
  @brief  Parse and sign extend the sample of the specified format

  @param  format Sample format to parse
  @param  buffer Buffer containing raw sample bytes
  @retval int32_t - Sign extended sample
*/
static int32_t raspdif_parse_sample(raspdif_format_t format, const uint8_t* buffer)
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
  @brief  Try to resize the input FIFO

  @param  fd File descriptor of the FIFO
  @param  size Requested size in bytes
  @retval bool - true when done (resized or failed for good), false to retry later
*/
static bool raspdif_set_pipe_size(int fd, int size)
{
  if (fcntl(fd, F_SETPIPE_SZ, size) < 0)
  {
    if (errno == EBUSY)
    {
      static bool reported = false;
      if (!reported)
        LOGI(TAG, "FIFO holds more than %d bytes, will resize once the writer pauses.", size);
      reported = true;
      return false;
    }

    LOGW(TAG, "Unable to set pipe size to %d. Error: %s.", size, strerror(errno));
    return true;
  }

  LOGI(TAG, "Input FIFO size: %d bytes.", fcntl(fd, F_GETPIPE_SZ));
  return true;
}

/**
  @brief  Index of the buffer the DMA is transmitting, from its control block address

  @param  none
  @retval uint32_t - 0..RASPDIF_BUFFER_COUNT-1
*/
static uint32_t raspdif_dma_index(void)
{
  uintptr_t current = (uintptr_t)bcm283x_dma_get_control_block(raspdif.dma_channel);
  uintptr_t first = (uintptr_t)&raspdif.control.bus->control_blocks[0];
  if (current < first)
    return 0;

  uintptr_t index = (current - first) / sizeof(dma_control_block_t);
  return index < RASPDIF_BUFFER_COUNT ? (uint32_t)index : 0;
}

/**
  @brief  Encode one whole buffer: n frames of input, the rest padding

          Padding is quiet noise with keep-alive (keeps receivers awake), zeros
          otherwise and always zeros while IEC 61937 data is flowing.

  @param  index Buffer to fill
  @param  frames Raw interleaved input frames (may be NULL when n is 0)
  @param  block SPDIF block for channel status
  @param  format Input sample format
  @param  n Number of input frames, at most RASPDIF_BUFFER_SIZE
  @param  keep_alive Pad with quiet noise instead of zeros
  @retval none
*/
static void raspdif_encode_buffer(uint32_t index, const uint8_t* frames, spdif_block_t* block, raspdif_format_t format, size_t n, bool keep_alive)
{
  raspdif_buffer_t* buffer = &raspdif.control.virtual->buffers[index];
  size_t sample_size = (format == raspdif_format_s24le) ? 3 : sizeof(int16_t);
  bool noise = keep_alive && !raspdif_non_audio;

  for (size_t i = 0; i < RASPDIF_BUFFER_SIZE; i++)
  {
    int32_t sample_a = 0;
    int32_t sample_b = 0;
    if (i < n)
    {
      sample_a = raspdif_parse_sample(format, &frames[i * 2 * sample_size]);
      sample_b = raspdif_parse_sample(format, &frames[i * 2 * sample_size + sample_size]);
    }
    else if (noise)
    {
      sample_a = (rand() % 10) - 5;
      sample_b = (rand() % 10) - 5;
    }

    // Padding counts too: after passthrough ends and the input pauses, the
    // non-audio flag must still time out before the next PCM arrives.
    raspdif_detect_iec61937(block, format, sample_a, sample_b);
    raspdif_buffer_samples(buffer, block, format, sample_a, sample_b);
  }
}

/**
  @brief  Sleep until the next tick on an absolute monotonic schedule

          Absolute deadlines do not drift; after a long stall the schedule
          restarts from now instead of firing a burst of catch-up ticks.

  @param  wake Deadline, advanced by step_ns
  @param  step_ns Tick length in nanoseconds
  @retval none
*/
static void raspdif_sleep_until(struct timespec* wake, long step_ns)
{
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);

  wake->tv_nsec += step_ns;
  while (wake->tv_nsec >= 1000000000L)
  {
    wake->tv_nsec -= 1000000000L;
    wake->tv_sec++;
  }

  if (wake->tv_sec < now.tv_sec || (wake->tv_sec == now.tv_sec && wake->tv_nsec < now.tv_nsec))
  {
    *wake = now;
    return;
  }

  clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, wake, NULL);
}

/**
  @brief  Run with real-time priority and locked memory

          With a small lead a late wake-up is audible (the DMA replays old
          data), so the scheduler must not let other work delay raspdif.

  @param  priority SCHED_FIFO priority, 0 to keep the normal scheduler
  @retval none
*/
static void raspdif_realtime(int priority)
{
  if (priority <= 0)
    return;

  struct sched_param param = {.sched_priority = priority};
  if (sched_setscheduler(0, SCHED_FIFO, &param) != 0)
    LOGW(TAG, "Unable to set SCHED_FIFO %d. Error: %s.", priority, strerror(errno));
  else
    LOGI(TAG, "Real-time priority %d (SCHED_FIFO).", priority);

  if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0)
    LOGW(TAG, "Unable to lock memory. Error: %s.", strerror(errno));
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
  // Line-buffer the log: under systemd stdout is a pipe and would otherwise
  // hold messages back until 4 KiB have accumulated.
  setvbuf(stdout, NULL, _IOLBF, 0);

  // Keep-alive noise
  srand(time(NULL));

  raspdif_arguments_t arguments;
  memset(&arguments, 0, sizeof(raspdif_arguments_t));

  // Set default sample rate, format and keep-alive
  arguments.sample_rate = RASPDIF_DEFAULT_SAMPLE_RATE;
  arguments.format = RASPDIF_DEFAULT_FORMAT;
  arguments.keep_alive = true;
  arguments.dma_channel = -1;
  arguments.rt_priority = 50;

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
  // Pi 4: channels 11-14 are DMA4 engines with a different register layout,
  // and the kernel allocates 0-10 from the bottom up. Channel 10 (DMA Lite,
  // plenty for S/PDIF) is the last one it would hand out; channel 5 was the
  // fourth.
  bool is_pi4 = bcm283x_is_model_pi4();
  if (is_pi4 && arguments.dma_channel > dma_channel_10)
    LOGF(TAG, "DMA channel %d is a DMA4 channel on the Pi 4 and not supported.", arguments.dma_channel);

  dma_channel_t dma_channel;
  if (arguments.dma_channel >= 0)
    dma_channel = (dma_channel_t)arguments.dma_channel;
  else
    dma_channel = is_pi4 ? dma_channel_10 : dma_channel_13;
  raspdif_init(dma_channel, arguments.sample_rate);

  // Allocate storage for a SPDIF block
  spdif_block_t block;
  memset(&block, 0, sizeof(block));

  // Populate each frame with channel status data
  spdif_populate_channel_status(&block);

  // Open the input: a file/FIFO or stdin
  size_t sample_size = (arguments.format == raspdif_format_s24le) ? 3 : sizeof(int16_t);
  input_t* input = input_open_fd(arguments.file, 2 * sample_size, 2 * RASPDIF_BUFFER_SIZE);

  if (input == NULL)
    LOGF(TAG, "Unable to open input.");
  raspdif.input = input;

  // A full FIFO is latency the writer (e.g. ALSA's file plugin) never reports.
  // The kernel refuses to shrink a pipe that holds more data than the new size
  // (EBUSY), e.g. when the writer kept playing across a restart of raspdif.
  // Then retry while transmitting; it succeeds once the writer pauses.
  bool pipe_size_pending = false;
  if (arguments.pipe_size > 0)
  {
    struct stat st;
    int fd = input_fd(input);
    if (fd >= 0 && fstat(fd, &st) == 0 && S_ISFIFO(st.st_mode))
      pipe_size_pending = !raspdif_set_pipe_size(fd, arguments.pipe_size);
    else
      LOGW(TAG, "--pipe-size given, but the input is not a FIFO.");
  }

  // Lead: number of buffers kept queued ahead of the DMA. It, not the ring
  // size, sets the latency; the rest of the ring is slack against late wake-ups.
  // At most COUNT - 2: "ahead" is taken modulo COUNT, so with COUNT - 1 the
  // loop would never see "enough queued" and would overwrite unplayed buffers.
  uint32_t lead = arguments.lead ? arguments.lead : RASPDIF_DEFAULT_LEAD;
  if (lead > RASPDIF_BUFFER_COUNT - 2)
    lead = RASPDIF_BUFFER_COUNT - 2;
  double buffer_s = RASPDIF_BUFFER_SIZE / arguments.sample_rate;
  LOGI(TAG, "Ring %d x %d frames, lead %u: output latency %.1f..%.1f ms.", RASPDIF_BUFFER_COUNT, RASPDIF_BUFFER_SIZE, lead,
       1e3 * (lead - 1) * buffer_s, 1e3 * lead * buffer_s);

  raspdif_realtime(arguments.rt_priority);

  // Fill the whole ring with silence, then start the DMA at control block 0
  for (uint32_t i = 0; i < RASPDIF_BUFFER_COUNT; i++)
    raspdif_encode_buffer(i, NULL, &block, arguments.format, 0, false);

  LOGI(TAG, "Transmitting...");
  bcm283x_dma_enable(raspdif.dma_channel, true);
  bcm283x_pcm_enable(true, false);

  uint32_t next = 1; // next buffer to fill
  uint32_t underruns = 0;
  uint32_t late = 0;
  bool streaming = false;
  uint8_t frames[RASPDIF_BUFFER_SIZE * 2 * sizeof(int32_t)];
  long tick_ns = (long)(1e9 * buffer_s / 2);
  struct timespec wake;
  clock_gettime(CLOCK_MONOTONIC, &wake);

  // --disable-pcm-on-idle: stop the PCM transmitter after a second without
  // input. The DMA then stalls on DREQ; the first input frame restarts it.
  bool pcm_off = false;
  uint32_t idle_buffers = 0;

  while (true)
  {
    uint32_t head = raspdif_dma_index();
    uint32_t ahead = (next + RASPDIF_BUFFER_COUNT - head) % RASPDIF_BUFFER_COUNT;
    if (ahead == 0)
    {
      // The DMA reached the buffer we were about to fill: we woke up too late
      // and it replayed old data. Continue right behind it.
      late++;
      LOGD(TAG, "Late wake-up (%u), resyncing.", late);
      next = (head + 1) % RASPDIF_BUFFER_COUNT;
      ahead = 1;
    }

    size_t available = input_available(input);
    bool eof = input_eof(input);

    if (pcm_off && available > 0)
    {
      bcm283x_pcm_enable(true, false);
      pcm_off = false;
      idle_buffers = 0;
      LOGD(TAG, "Input again, PCM enabled.");
    }

    // Enough queued: sleep. Input short but a queued buffer still in hand:
    // wait for the writer instead of padding with silence.
    if (ahead > lead || (available < RASPDIF_BUFFER_SIZE && ahead > 1 && !eof))
    {
      if (pipe_size_pending)
        pipe_size_pending = !raspdif_set_pipe_size(input_fd(input), arguments.pipe_size);

      raspdif_sleep_until(&wake, tick_ns);
      continue;
    }

    if (eof && available == 0)
      break;

    size_t n = input_read(input, frames, RASPDIF_BUFFER_SIZE);
    if (n < RASPDIF_BUFFER_SIZE && streaming && !eof) // a short last buffer is no underrun
    {
      underruns++;
      LOGD(TAG, "Buffer underrun (%u), padded %zu frames.", underruns, RASPDIF_BUFFER_SIZE - n);
    }
    streaming = n > 0;

    raspdif_encode_buffer(next, frames, &block, arguments.format, n, arguments.keep_alive);
    next = (next + 1) % RASPDIF_BUFFER_COUNT;

    idle_buffers = n > 0 ? 0 : idle_buffers + 1;
    if (arguments.pcm_disable && !pcm_off && idle_buffers * buffer_s >= 1.0)
    {
      bcm283x_pcm_enable(false, false);
      pcm_off = true;
      LOGD(TAG, "No input for a second, PCM disabled.");
    }
  }

  // End of a file or stdin: queue silence behind the last audio so the DMA
  // does not replay old ring contents, then let everything play out (#4)
  uint32_t head = raspdif_dma_index();
  for (uint32_t i = 0; i <= lead && (next + RASPDIF_BUFFER_COUNT - head) % RASPDIF_BUFFER_COUNT != 0; i++)
  {
    raspdif_encode_buffer(next, NULL, &block, arguments.format, 0, false);
    next = (next + 1) % RASPDIF_BUFFER_COUNT;
  }
  double tail_s = (lead + 1) * buffer_s; // may exceed a second: no microsleep (asserts < 1 s)
  struct timespec tail = {.tv_sec = (time_t)tail_s, .tv_nsec = (long)((tail_s - (time_t)tail_s) * 1e9)};
  clock_nanosleep(CLOCK_MONOTONIC, 0, &tail, NULL);

  // Shutdown in a safe manner (also closes the input)
  raspdif_shutdown();

  return 0;
}
