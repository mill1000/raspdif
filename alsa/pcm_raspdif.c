/*
  ALSA external PCM plugin for raspdif.

  Gives applications (e.g. Kodi) a real ALSA playback device that writes into
  raspdif's shared-memory ring and reports the true output delay, so players can
  keep lip sync themselves. raspdif must be running with --shm before the device
  is opened.

  asound.conf:
    pcm_type.raspdif {
      lib "/path/to/libasound_module_pcm_raspdif.so"
    }
    pcm.hdmi_raspdif {
      type raspdif
      hint { show on description "S/PDIF (raspdif)" }
    }

  S16_LE, 2 channels, 44.1 or 48 kHz (raspdif retunes its clock on request).
*/

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include "raspdif_shm.h"

#define FRAME_BYTES          (RASPDIF_SHM_CHANNELS * sizeof(int16_t))
#define MIN_PERIOD_BYTES     (64 * FRAME_BYTES)
#define MAX_BUFFER_BYTES     ((RASPDIF_SHM_RING_FRAMES / 2) * FRAME_BYTES)
#define HEARTBEAT_TIMEOUT_NS 500000000LL // raspdif counts up every ~1 ms

typedef struct raspdif_plugin_t
{
  snd_pcm_ioplug_t io;
  raspdif_shm_t* shm;
  int shm_fd;
  int timer_fd;
  bool locked;        // this instance owns the ring (flock)
  bool attached;      // ring reset and rate set for the current stream
  bool running;       // started: raspdif may take what we write
  uint32_t base;      // shared write position at attach
  uint64_t written;   // frames written since prepare
  uint64_t consumed;  // frames raspdif has taken since attach
  uint32_t last_read; // shared read position last seen
  uint32_t last_heartbeat;
  int64_t heartbeat_ns; // when the heartbeat last moved
} raspdif_plugin_t;

static int64_t now_ns(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static uint32_t load32(uint32_t* p)
{
  return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static bool daemon_alive(raspdif_shm_t* shm)
{
  uint32_t pid = load32(&shm->daemon_pid);
  return pid != 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM);
}

// False once raspdif's heartbeat has stood still for HEARTBEAT_TIMEOUT_NS
static bool daemon_running(raspdif_plugin_t* rd)
{
  uint32_t hb = load32(&rd->shm->heartbeat);
  int64_t now = now_ns();
  if (hb != rd->last_heartbeat)
  {
    rd->last_heartbeat = hb;
    rd->heartbeat_ns = now;
    return true;
  }
  return now - rd->heartbeat_ns < HEARTBEAT_TIMEOUT_NS;
}

// Make written frames visible to raspdif. Before start() nothing is published:
// ALSA assumes the hardware consumes nothing until the stream starts, and a
// reader that runs ahead would leave the prefill stuck short of the start threshold.
static void publish(raspdif_plugin_t* rd)
{
  if (rd->running)
    __atomic_store_n(&rd->shm->write_pos, (uint32_t)(rd->base + rd->written), __ATOMIC_RELEASE);
}

// Ask raspdif to drop everything published so far (and only that)
static void discard(raspdif_plugin_t* rd)
{
  __atomic_store_n(&rd->shm->discard_pos, load32(&rd->shm->write_pos), __ATOMIC_RELEASE);
  __atomic_add_fetch(&rd->shm->discard_seq, 1, __ATOMIC_RELEASE);
}

// Track what raspdif has taken since attach (32 bit positions, 64 bit total)
static uint64_t update_consumed(raspdif_plugin_t* rd)
{
  if (!rd->attached)
    return 0;

  uint32_t r = load32(&rd->shm->read_pos);
  int32_t step = (int32_t)(r - rd->last_read);
  if (step > 0)
  {
    rd->consumed += (uint32_t)step;
    rd->last_read = r;
  }
  if (rd->consumed > rd->written)
    rd->consumed = rd->written;
  return rd->consumed;
}

static int timer_arm(raspdif_plugin_t* rd, bool on)
{
  struct itimerspec spec;
  memset(&spec, 0, sizeof(spec));
  if (on)
  {
    // Wake about four times per period, but not faster than every millisecond
    long ns = (long)(1e9 * rd->io.period_size / rd->io.rate / 4);
    if (ns < 1000000)
      ns = 1000000;
    spec.it_interval.tv_sec = ns / 1000000000L;
    spec.it_interval.tv_nsec = ns % 1000000000L;
    spec.it_value = spec.it_interval;
  }
  return timerfd_settime(rd->timer_fd, 0, &spec, NULL);
}

// Become the ring's writer for this stream: lock, set the rate, drop old audio.
// Done on the first write or start, not at prepare: snd_pcm_hw_params() prepares
// too, and a program that only probes the device must not disturb a running one.
static int attach(raspdif_plugin_t* rd)
{
  if (rd->attached)
    return 0;

  if (!rd->locked)
  {
    if (flock(rd->shm_fd, LOCK_EX | LOCK_NB) != 0)
      return -EBUSY;
    rd->locked = true;
  }

  if (!daemon_alive(rd->shm))
    return -ENODEV;

  // Ask raspdif for our rate and wait until it transmits at it
  __atomic_store_n(&rd->shm->requested_rate, rd->io.rate, __ATOMIC_RELEASE);
  for (int i = 0; load32(&rd->shm->rate) != rd->io.rate; i++)
  {
    if (i >= 150) // 300 ms
      return -EIO;
    usleep(2000);
  }

  discard(rd);
  rd->base = load32(&rd->shm->write_pos);
  rd->last_read = rd->base;
  rd->consumed = 0;
  rd->last_heartbeat = load32(&rd->shm->heartbeat);
  rd->heartbeat_ns = now_ns();
  rd->attached = true;
  return 0;
}

static int rd_start(snd_pcm_ioplug_t* io)
{
  raspdif_plugin_t* rd = io->private_data;
  int err = attach(rd);
  if (err < 0)
    return err;
  rd->running = true;
  publish(rd);
  return 0;
}

static int rd_stop(snd_pcm_ioplug_t* io)
{
  raspdif_plugin_t* rd = io->private_data;
  rd->running = false;
  // Drop what raspdif has not taken yet (pause/stop must be silent quickly)
  if (rd->attached)
    discard(rd);
  rd->attached = false;
  timer_arm(rd, false);
  return 0;
}

static snd_pcm_sframes_t rd_pointer(snd_pcm_ioplug_t* io)
{
  raspdif_plugin_t* rd = io->private_data;
  if (rd->attached)
  {
    if (!daemon_running(rd))
      return -EPIPE;
    // A restarted raspdif forgets old rate requests and transmits at its
    // --rate: keep asking for ours while the stream runs.
    if (load32(&rd->shm->requested_rate) != io->rate)
      __atomic_store_n(&rd->shm->requested_rate, io->rate, __ATOMIC_RELEASE);
  }
  return (snd_pcm_sframes_t)(update_consumed(rd) % io->buffer_size);
}

static snd_pcm_sframes_t rd_transfer(snd_pcm_ioplug_t* io, const snd_pcm_channel_area_t* areas, snd_pcm_uframes_t offset,
                                     snd_pcm_uframes_t size)
{
  raspdif_plugin_t* rd = io->private_data;
  raspdif_shm_t* shm = rd->shm;

  int err = attach(rd);
  if (err < 0)
    return err;

  // RW_INTERLEAVED, S16_LE, 2 channels: one area, frames are contiguous
  const uint8_t* src = (const uint8_t*)areas[0].addr + (areas[0].first + offset * areas[0].step) / 8;

  uint32_t w = (uint32_t)(rd->base + rd->written);
  for (snd_pcm_uframes_t i = 0; i < size; i++)
  {
    size_t idx = (size_t)((uint32_t)(w + i) % RASPDIF_SHM_RING_FRAMES) * RASPDIF_SHM_CHANNELS;
    memcpy(&shm->ring[idx], src + i * FRAME_BYTES, FRAME_BYTES);
  }

  rd->written += size;
  publish(rd);
  return (snd_pcm_sframes_t)size;
}

static int rd_prepare(snd_pcm_ioplug_t* io)
{
  raspdif_plugin_t* rd = io->private_data;

  if (rd->attached)
    discard(rd);
  rd->attached = false;
  rd->running = false;
  rd->written = 0;
  rd->consumed = 0;
  return timer_arm(rd, true) == 0 ? 0 : -errno;
}

static int rd_delay(snd_pcm_ioplug_t* io, snd_pcm_sframes_t* delay)
{
  raspdif_plugin_t* rd = io->private_data;
  uint64_t pending = rd->written - update_consumed(rd);
  uint32_t queued = rd->attached ? load32(&rd->shm->queued_frames) : 0;
  *delay = (snd_pcm_sframes_t)(pending + queued);
  return 0;
}

static int rd_poll_revents(snd_pcm_ioplug_t* io, struct pollfd* pfd, unsigned int nfds, unsigned short* revents)
{
  raspdif_plugin_t* rd = io->private_data;
  (void)nfds;

  uint64_t ticks;
  if (pfd[0].revents & POLLIN)
  {
    ssize_t n = read(rd->timer_fd, &ticks, sizeof(ticks));
    (void)n;
  }

  // Writable once a period fits. (Querying sw_params here would re-enter
  // alsa-lib's PCM lock; ALSA's wait loop re-checks avail_min itself.)
  snd_pcm_uframes_t used = (snd_pcm_uframes_t)(rd->written - update_consumed(rd));
  snd_pcm_uframes_t avail = used >= io->buffer_size ? 0 : io->buffer_size - used;

  *revents = avail >= io->period_size ? POLLOUT : 0;
  return 0;
}

static int rd_close(snd_pcm_ioplug_t* io)
{
  raspdif_plugin_t* rd = io->private_data;
  if (rd->attached)
    discard(rd);
  if (rd->shm)
    munmap(rd->shm, sizeof(raspdif_shm_t));
  if (rd->shm_fd >= 0)
    close(rd->shm_fd); // also releases the flock
  if (rd->timer_fd >= 0)
    close(rd->timer_fd);
  free(rd);
  return 0;
}

static const snd_pcm_ioplug_callback_t rd_callback = {
  .start = rd_start,
  .stop = rd_stop,
  .pointer = rd_pointer,
  .transfer = rd_transfer,
  .prepare = rd_prepare,
  .delay = rd_delay,
  .poll_revents = rd_poll_revents,
  .close = rd_close,
};

static int rd_set_constraints(raspdif_plugin_t* rd)
{
  static const unsigned int access[] = {SND_PCM_ACCESS_RW_INTERLEAVED};
  static const unsigned int formats[] = {SND_PCM_FORMAT_S16_LE};
  // raspdif retunes its clock on request. More would be possible (up to 96 kHz),
  // but cheap TOSLINK transmitters are not specified for the higher cell rates.
  static const unsigned int rates[] = {44100, 48000};
  int err;

  if ((err = snd_pcm_ioplug_set_param_list(&rd->io, SND_PCM_IOPLUG_HW_ACCESS, 1, access)) < 0 ||
      (err = snd_pcm_ioplug_set_param_list(&rd->io, SND_PCM_IOPLUG_HW_FORMAT, 1, formats)) < 0 ||
      (err = snd_pcm_ioplug_set_param_minmax(&rd->io, SND_PCM_IOPLUG_HW_CHANNELS, RASPDIF_SHM_CHANNELS, RASPDIF_SHM_CHANNELS)) < 0 ||
      (err = snd_pcm_ioplug_set_param_list(&rd->io, SND_PCM_IOPLUG_HW_RATE, 2, rates)) < 0 ||
      (err = snd_pcm_ioplug_set_param_minmax(&rd->io, SND_PCM_IOPLUG_HW_PERIOD_BYTES, MIN_PERIOD_BYTES, MAX_BUFFER_BYTES / 2)) < 0 ||
      (err = snd_pcm_ioplug_set_param_minmax(&rd->io, SND_PCM_IOPLUG_HW_PERIODS, 2, 64)) < 0 ||
      (err = snd_pcm_ioplug_set_param_minmax(&rd->io, SND_PCM_IOPLUG_HW_BUFFER_BYTES, 2 * MIN_PERIOD_BYTES, MAX_BUFFER_BYTES)) < 0)
    return err;
  return 0;
}

SND_PCM_PLUGIN_DEFINE_FUNC(raspdif)
{
  snd_config_iterator_t i, next;
  (void)root;

  snd_config_for_each(i, next, conf)
  {
    snd_config_t* n = snd_config_iterator_entry(i);
    const char* id;
    if (snd_config_get_id(n, &id) < 0)
      continue;
    if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 || strcmp(id, "hint") == 0)
      continue;
    SNDERR("Unknown field %s", id);
    return -EINVAL;
  }

  if (stream != SND_PCM_STREAM_PLAYBACK)
    return -EINVAL;

  raspdif_plugin_t* rd = calloc(1, sizeof(*rd));
  if (!rd)
    return -ENOMEM;
  rd->shm_fd = -1;
  rd->timer_fd = -1;

  int err = -ENODEV;
  rd->shm_fd = open(RASPDIF_SHM_PATH, O_RDWR | O_CLOEXEC | O_NOFOLLOW);
  if (rd->shm_fd < 0)
  {
    err = errno == EACCES ? -EACCES : -ENODEV;
    goto fail;
  }

  // Only a ring created by raspdif (root) is trusted
  struct stat st;
  if (fstat(rd->shm_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 || st.st_size < (off_t)sizeof(raspdif_shm_t))
    goto fail;

  void* map = mmap(NULL, sizeof(raspdif_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, rd->shm_fd, 0);
  if (map == MAP_FAILED)
    goto fail;
  rd->shm = map;

  if (load32(&rd->shm->magic) != RASPDIF_SHM_MAGIC || rd->shm->version != RASPDIF_SHM_VERSION ||
      rd->shm->ring_frames != RASPDIF_SHM_RING_FRAMES || !daemon_alive(rd->shm))
    goto fail;

  rd->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
  if (rd->timer_fd < 0)
  {
    err = -errno;
    goto fail;
  }

  rd->io.version = SND_PCM_IOPLUG_VERSION;
  rd->io.name = "raspdif S/PDIF";
  rd->io.callback = &rd_callback;
  rd->io.private_data = rd;
  rd->io.poll_fd = rd->timer_fd;
  rd->io.poll_events = POLLIN;
  rd->io.mmap_rw = 0;

  err = snd_pcm_ioplug_create(&rd->io, name, stream, mode);
  if (err < 0)
    goto fail;

  err = rd_set_constraints(rd);
  if (err < 0)
  {
    snd_pcm_ioplug_delete(&rd->io); // calls rd_close
    return err;
  }

  *pcmp = rd->io.pcm;
  return 0;

fail:
  if (rd->shm)
    munmap(rd->shm, sizeof(raspdif_shm_t));
  if (rd->shm_fd >= 0)
    close(rd->shm_fd);
  if (rd->timer_fd >= 0)
    close(rd->timer_fd);
  free(rd);
  return err;
}

SND_PCM_PLUGIN_SYMBOL(raspdif);
