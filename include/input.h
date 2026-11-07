#ifndef __INPUT__
#define __INPUT__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Input source for raspdif: a file descriptor (stdin, file or FIFO) read with
// non-blocking read() into a small private ring. Frames are raw interleaved
// stereo samples.

typedef struct input_t input_t;

input_t* input_open_fd(const char* path, size_t frame_bytes, size_t max_frames); // path NULL = stdin
void input_close(input_t* input);

size_t input_available(input_t* input);                      // frames ready now
size_t input_read(input_t* input, uint8_t* dst, size_t max); // copies whole frames
bool input_eof(input_t* input);                              // regular file/stdin drained

int input_fd(input_t* input);

#endif
