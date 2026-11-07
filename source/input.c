#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"
#include "raspdif_shm.h"

#define TAG "INPUT"

struct input_t
{
  bool shm;
  size_t frame_bytes;

  // fd mode
  int fd;
  bool eof;
  uint8_t* ring;     // ring_bytes
  size_t ring_bytes; // small on purpose: anything buffered here is latency
  size_t head;       // read index
  size_t fill;       // bytes stored

  // shm mode
  raspdif_shm_t* map;
  uint32_t discard_seen;
};

/**
  @brief  Frames ready in the shared ring; honours discard requests of the writer
*/
static size_t input_shm_pending(input_t* input)
{
  raspdif_shm_t* map = input->map;

  uint32_t seq = __atomic_load_n(&map->discard_seq, __ATOMIC_ACQUIRE);
  if (seq != input->discard_seen)
  {
    input->discard_seen = seq;
    uint32_t to = __atomic_load_n(&map->discard_pos, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&map->read_pos, __ATOMIC_RELAXED);
    if ((int32_t)(to - r) > 0)
      __atomic_store_n(&map->read_pos, to, __ATOMIC_RELEASE);
  }

  uint32_t w = __atomic_load_n(&map->write_pos, __ATOMIC_ACQUIRE);
  uint32_t r = __atomic_load_n(&map->read_pos, __ATOMIC_RELAXED);
  int32_t n = (int32_t)(w - r);
  if (n <= 0)
    return 0;

  // A writer that ran ahead by more than the ring (should not happen) is clamped
  return n > RASPDIF_SHM_RING_FRAMES ? RASPDIF_SHM_RING_FRAMES : (size_t)n;
}

/**
  @brief  Open stdin, a regular file or a FIFO as input

          FIFOs are opened read-write so they never report EOF when the writer
          goes away; regular files and stdin end at EOF. Only up to max_frames
          are read ahead, so the private buffer adds little latency.

  @param  path File to open, NULL for stdin
  @param  frame_bytes Size of one stereo frame in bytes
  @param  max_frames Frames to buffer at most
  @retval input_t* - NULL on failure
*/
input_t* input_open_fd(const char* path, size_t frame_bytes, size_t max_frames)
{
  int fd = 0;
  if (path)
  {
    struct stat st;
    bool fifo = stat(path, &st) == 0 && S_ISFIFO(st.st_mode);
    fd = open(path, fifo ? O_RDWR : O_RDONLY);
    if (fd < 0)
    {
      LOGE(TAG, "Unable to open %s. Error: %s.", path, strerror(errno));
      return NULL;
    }
  }

  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

  input_t* input = calloc(1, sizeof(input_t));
  if (input == NULL)
    return NULL;
  input->ring_bytes = max_frames * frame_bytes;
  input->ring = malloc(input->ring_bytes);
  if (input->ring == NULL)
  {
    free(input);
    return NULL;
  }
  input->fd = fd;
  input->frame_bytes = frame_bytes;
  return input;
}

/**
  @brief  Group id of the "audio" group from /etc/group, -1 if there is none

          Parsed by hand: getgrnam() in a static binary drags in NSS.
*/
static gid_t input_audio_gid(void)
{
  FILE* f = fopen("/etc/group", "r");
  if (f == NULL)
    return (gid_t)-1;

  char line[512];
  gid_t gid = (gid_t)-1;
  while (fgets(line, sizeof(line), f))
  {
    unsigned int id;
    if (strncmp(line, "audio:", 6) == 0 && sscanf(line, "audio:%*[^:]:%u:", &id) == 1)
    {
      gid = (gid_t)id;
      break;
    }
  }
  fclose(f);
  return gid;
}

/**
  @brief  Open the shared-memory file safely

          /dev/shm is world-writable: never follow a symlink and never reuse a
          file somebody else created; such a file is removed and recreated.
*/
static int input_open_shm_file(void)
{
  for (int attempt = 0; attempt < 2; attempt++)
  {
    int flags = O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC | (attempt ? O_EXCL : 0);
    int fd = open(RASPDIF_SHM_PATH, flags, 0600);
    if (fd < 0)
    {
      LOGE(TAG, "Unable to open %s. Error: %s.", RASPDIF_SHM_PATH, strerror(errno));
      return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid())
      return fd;

    LOGW(TAG, "%s is not a regular file of ours, recreating it.", RASPDIF_SHM_PATH);
    close(fd);
    unlink(RASPDIF_SHM_PATH);
  }
  return -1;
}

/**
  @brief  Create or attach to the shared-memory ring of the ALSA plugin

          The ring survives restarts of raspdif, so a writer keeps working.
          Anything queued before we started is dropped (read_pos = write_pos),
          and so is a rate request left over from an earlier writer.

  @param  rate Sample rate raspdif transmits at
  @retval input_t* - NULL on failure
*/
input_t* input_open_shm(uint32_t rate)
{
  int fd = input_open_shm_file();
  if (fd < 0)
    return NULL;

  // Members of "audio" may play; without that group only root
  gid_t gid = input_audio_gid();
  if (gid != (gid_t)-1 && fchown(fd, (uid_t)-1, gid) == 0)
    fchmod(fd, 0660);
  else
    fchmod(fd, 0600);

  if (ftruncate(fd, sizeof(raspdif_shm_t)) != 0)
  {
    LOGE(TAG, "Unable to size %s. Error: %s.", RASPDIF_SHM_PATH, strerror(errno));
    close(fd);
    return NULL;
  }

  raspdif_shm_t* map = mmap(NULL, sizeof(raspdif_shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
  if (map == MAP_FAILED)
  {
    LOGE(TAG, "Unable to map %s. Error: %s.", RASPDIF_SHM_PATH, strerror(errno));
    return NULL;
  }

  if (map->magic != RASPDIF_SHM_MAGIC || map->version != RASPDIF_SHM_VERSION || map->ring_frames != RASPDIF_SHM_RING_FRAMES)
  {
    memset(map, 0, sizeof(raspdif_shm_t));
    map->version = RASPDIF_SHM_VERSION;
    map->ring_frames = RASPDIF_SHM_RING_FRAMES;
    __atomic_store_n(&map->magic, RASPDIF_SHM_MAGIC, __ATOMIC_RELEASE);
    LOGI(TAG, "Initialized %s.", RASPDIF_SHM_PATH);
  }

  input_t* input = calloc(1, sizeof(input_t));
  if (input == NULL)
  {
    munmap(map, sizeof(raspdif_shm_t));
    return NULL;
  }

  __atomic_store_n(&map->rate, rate, __ATOMIC_RELEASE);
  __atomic_store_n(&map->requested_rate, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&map->read_pos, __atomic_load_n(&map->write_pos, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
  __atomic_store_n(&map->queued_frames, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&map->daemon_pid, (uint32_t)getpid(), __ATOMIC_RELEASE);

  input->shm = true;
  input->fd = -1;
  input->frame_bytes = RASPDIF_SHM_CHANNELS * sizeof(int16_t);
  input->map = map;
  input->discard_seen = __atomic_load_n(&map->discard_seq, __ATOMIC_ACQUIRE);
  return input;
}

void input_close(input_t* input)
{
  if (!input)
    return;

  if (input->shm)
  {
    __atomic_store_n(&input->map->daemon_pid, 0, __ATOMIC_RELEASE);
    munmap(input->map, sizeof(raspdif_shm_t));
  }
  else
  {
    if (input->fd > 0)
      close(input->fd);
    free(input->ring);
  }
  free(input);
}

/**
  @brief  Pull what the fd has into the private ring, up to its size (non-blocking)
*/
static void input_fill_fd(input_t* input)
{
  while (!input->eof && input->fill < input->ring_bytes)
  {
    size_t tail = (input->head + input->fill) % input->ring_bytes;
    size_t space = input->ring_bytes - input->fill;
    size_t chunk = tail + space > input->ring_bytes ? input->ring_bytes - tail : space;

    ssize_t n = read(input->fd, input->ring + tail, chunk);
    if (n > 0)
    {
      input->fill += n;
      continue;
    }
    if (n == 0)
      input->eof = true;
    else if (errno != EAGAIN && errno != EINTR)
    {
      LOGW(TAG, "Read failed. Error: %s.", strerror(errno));
      input->eof = true;
    }
    break;
  }
}

size_t input_available(input_t* input)
{
  if (input->shm)
    return input_shm_pending(input);

  input_fill_fd(input);
  return input->fill / input->frame_bytes;
}

size_t input_read(input_t* input, uint8_t* dst, size_t max)
{
  size_t frames = input_available(input);
  if (frames > max)
    frames = max;

  if (input->shm)
  {
    raspdif_shm_t* map = input->map;
    uint32_t w = __atomic_load_n(&map->write_pos, __ATOMIC_ACQUIRE);
    uint32_t r = __atomic_load_n(&map->read_pos, __ATOMIC_RELAXED);
    if ((int32_t)(w - r) > RASPDIF_SHM_RING_FRAMES) // overrun: skip to the oldest valid frame
      r = w - RASPDIF_SHM_RING_FRAMES;

    for (size_t i = 0; i < frames; i++)
    {
      size_t idx = (size_t)((uint32_t)(r + i) % RASPDIF_SHM_RING_FRAMES) * RASPDIF_SHM_CHANNELS;
      memcpy(dst + i * input->frame_bytes, &map->ring[idx], input->frame_bytes);
    }
    __atomic_store_n(&map->read_pos, (uint32_t)(r + frames), __ATOMIC_RELEASE);
    return frames;
  }

  size_t bytes = frames * input->frame_bytes;
  for (size_t i = 0; i < bytes; i++)
    dst[i] = input->ring[(input->head + i) % input->ring_bytes];
  input->head = (input->head + bytes) % input->ring_bytes;
  input->fill -= bytes;
  return frames;
}

bool input_eof(input_t* input)
{
  return !input->shm && input->eof && input->fill < input->frame_bytes;
}

int input_fd(input_t* input)
{
  return input->shm ? -1 : input->fd;
}

uint32_t input_requested_rate(input_t* input)
{
  return input->shm ? __atomic_load_n(&input->map->requested_rate, __ATOMIC_ACQUIRE) : 0;
}

void input_confirm_rate(input_t* input, uint32_t rate)
{
  if (input->shm)
    __atomic_store_n(&input->map->rate, rate, __ATOMIC_RELEASE);
}

void input_publish(input_t* input, uint32_t queued)
{
  if (!input->shm)
    return;

  __atomic_store_n(&input->map->queued_frames, queued, __ATOMIC_RELEASE);
  __atomic_add_fetch(&input->map->heartbeat, 1, __ATOMIC_RELEASE);
}
