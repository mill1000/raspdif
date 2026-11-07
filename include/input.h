#ifndef __INPUT__
#define __INPUT__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Input source for raspdif: a file descriptor (stdin, file or FIFO) read with
// non-blocking read() into a private byte ring, or the shared-memory ring the
// ALSA plugin writes to. Frames are raw interleaved stereo samples.

typedef struct input_t input_t;

input_t* input_open_fd(const char* path, size_t frame_bytes, size_t max_frames); // path NULL = stdin
input_t* input_open_shm(uint32_t rate);
void input_close(input_t* input);

size_t input_available(input_t* input);                      // frames ready now
size_t input_read(input_t* input, uint8_t* dst, size_t max); // copies whole frames
bool input_eof(input_t* input);                              // regular file/stdin drained

int input_fd(input_t* input);                           // -1 for shm
void input_publish(input_t* input, uint32_t queued);    // shm: delay + heartbeat
uint32_t input_requested_rate(input_t* input);          // shm: rate the writer wants, 0 = none
void input_confirm_rate(input_t* input, uint32_t rate); // shm: rate now transmitted

#endif
