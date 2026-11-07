#ifndef __RASPDIF_SHM__
#define __RASPDIF_SHM__

// Shared-memory interface between raspdif and its ALSA plugin.
//
// The plugin (one writer) appends S16_LE stereo frames to the ring and advances
// write_pos; raspdif (one reader) takes them for the DMA and advances read_pos.
// Positions are free-running 32 bit frame counters (they wrap after ~24 h at
// 48 kHz); compare them only through differences cast to int32_t. The ring
// index is pos % ring_frames, which stays consistent across the wrap because
// ring_frames divides 2^32. 32 bit keeps every access atomic without libatomic,
// also on 32 bit ARM.
//
// queued_frames is the audio raspdif has already taken but not yet sent on the
// wire, so the true output delay seen by the writer is
//   (write_pos - read_pos) + queued_frames.
//
// To drop unplayed audio (stop/pause) the writer stores the position up to
// which to drop in discard_pos, then increments discard_seq; the reader then
// advances read_pos to discard_pos (never backwards). A bounded position, not
// "everything", so audio written after a quick restart is not lost.
//
// Sample rate: the writer stores the rate it wants in requested_rate; raspdif
// retunes its clock and confirms by updating rate. The writer waits for that
// before it writes audio.
//
// raspdif increments heartbeat on every loop; a writer that sees it stand still
// treats raspdif as gone. Each field has exactly one writer. All fields are
// accessed with __atomic builtins (acquire/release).
//
// The file is created by raspdif (root) with mode 0660 and group "audio" when
// that group exists (0600 otherwise); the plugin refuses a file not owned by root.

#include <stdint.h>

#define RASPDIF_SHM_PATH        "/dev/shm/raspdif"
#define RASPDIF_SHM_MAGIC       0x46445352 // "RSDF"
#define RASPDIF_SHM_VERSION     4
#define RASPDIF_SHM_RING_FRAMES 16384 // power of two, ~341 ms at 48 kHz
#define RASPDIF_SHM_CHANNELS    2

typedef struct raspdif_shm_t
{
  uint32_t magic;
  uint32_t version;
  uint32_t rate;           // sample rate raspdif transmits at (raspdif writes)
  uint32_t ring_frames;    // RASPDIF_SHM_RING_FRAMES
  uint32_t write_pos;      // frames written by the plugin
  uint32_t read_pos;       // frames taken by raspdif
  uint32_t queued_frames;  // taken but not yet transmitted (raspdif)
  uint32_t heartbeat;      // incremented by raspdif on every loop
  uint32_t daemon_pid;     // raspdif process id, 0 when not running
  uint32_t discard_seq;    // writer: increment after setting discard_pos
  uint32_t discard_pos;    // writer: drop everything before this position
  uint32_t requested_rate; // writer: rate it wants, 0 = no request
  uint32_t _reserved[8];
  int16_t ring[RASPDIF_SHM_RING_FRAMES * RASPDIF_SHM_CHANNELS];
} raspdif_shm_t;

#endif
