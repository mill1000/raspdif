#ifndef __BCM283X_MAILBOX__
#define __BCM283X_MAILBOX__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define MAILBOX_CODE_SUCCESS  0x80000000

typedef enum
{
  MAILBOX_MEM_FLAG_DISCARDABLE = 1 << 0,
  MAILBOX_MEM_FLAG_NORMAL = 0, // Do not use from ARM
  MAILBOX_MEM_FLAG_DIRECT = 1 << 2, // 0xC alias
  MAILBOX_MEM_FLAG_COHERENT = 2 << 2, // 0x8 alias
  MAILBOX_MEM_FLAG_L1_NONALLOCATING = (MAILBOX_MEM_FLAG_DIRECT | MAILBOX_MEM_FLAG_COHERENT),
  MAILBOX_MEM_FLAG_ZERO_INIT = 1 << 4,
  MAILBOX_MEM_FLAG_NO_INIT = 1 << 5,
  MAILBOX_FLAG_HINT_PERMALOCK = 1 << 6,
} MAILBOX_MEM_FLAG;

typedef struct mailbox_message_header_t
{
  uint32_t length; // Total message length
  uint32_t code; // Message request/response code
} mailbox_message_header_t;

typedef struct mailbox_message_trailer_t
{
  uint32_t end;
} mailbox_message_trailer_t;

typedef enum
{
  mailbox_tag_id_get_dma_channels = 0x00060001,
  mailbox_tag_id_allocate_memory  = 0x0003000c,
  mailbox_tag_id_lock_memory      = 0x0003000d,
  mailbox_tag_id_unlock_memory    = 0x0003000e,
  mailbox_tag_id_release_memory   = 0x0003000f,
} mailbox_tag_id_t;

typedef struct mailbox_tag_header_t
{
  uint32_t identifier;
  uint32_t length; // Length of value buffer
  uint32_t code; // Tag request/Response Code
} mailbox_tag_header_t;

typedef struct mailbox_dma_channel_request_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t pad;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_dma_channel_request_t;

typedef struct mailbox_dma_channel_response_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t mask;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_dma_channel_response_t;

typedef struct mailbox_message_allocate_memory_request_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t size;
    uint32_t alignment;
    uint32_t flags;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_message_allocate_memory_request_t;

typedef struct mailbox_message_allocate_memory_response_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t handle;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_message_allocate_memory_response_t;

typedef struct mailbox_tag_lock_memory_request_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t handle;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_lock_memory_request_t;

typedef struct mailbox_tag_lock_memory_response_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t busAddress;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_lock_memory_response_t;

typedef struct mailbox_tag_unlock_memory_request_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t handle;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_unlock_memory_request_t;

typedef struct mailbox_tag_unlock_memory_response_t
{
  mailbox_message_header_t header;
  struct
  {
    mailbox_tag_header_t header;
    uint32_t status;
  } tag;
  mailbox_message_trailer_t trailer;
} mailbox_unlock_memory_response_t;

typedef mailbox_unlock_memory_request_t mailbox_release_memory_request_t;
typedef mailbox_unlock_memory_response_t mailbox_release_memory_response_t;

#endif
