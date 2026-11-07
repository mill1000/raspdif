#include "input.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "log.h"

#define TAG "INPUT"

struct input_t
{
  size_t frame_bytes;
  int fd;
  bool eof;
  uint8_t* ring;     // ring_bytes
  size_t ring_bytes; // small on purpose: anything buffered here is latency
  size_t head;       // read index
  size_t fill;       // bytes stored
};

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

void input_close(input_t* input)
{
  if (!input)
    return;

  if (input->fd > 0)
    close(input->fd);
  free(input->ring);
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
  input_fill_fd(input);
  return input->fill / input->frame_bytes;
}

size_t input_read(input_t* input, uint8_t* dst, size_t max)
{
  size_t frames = input_available(input);
  if (frames > max)
    frames = max;

  size_t bytes = frames * input->frame_bytes;
  for (size_t i = 0; i < bytes; i++)
    dst[i] = input->ring[(input->head + i) % input->ring_bytes];
  input->head = (input->head + bytes) % input->ring_bytes;
  input->fill -= bytes;
  return frames;
}

bool input_eof(input_t* input)
{
  return input->eof && input->fill < input->frame_bytes;
}

int input_fd(input_t* input)
{
  return input->fd;
}
