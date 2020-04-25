#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <assert.h>

#include "mailbox.h"
#include "log.h"

#define TAG "Mailbox"

/**
  @brief  Send a mailbox message via IOCTL interface

  @param  buffer Buffer to send and receiving into
  @retval int32_t - Result of ioctl
*/
static int32_t mailboxSend(void* buffer)
{
   assert(buffer != NULL);
   
   int32_t mbox = open("/dev/vcio", O_RDONLY);
   if (mbox < 0)
   {
      LOGF(TAG, "Failed to open /dev/vcio. Error: %s", strerror(errno));
      return -1;
   }

   int32_t result = ioctl(mbox, IOCTL_MBOX_PROPERTY, buffer);
   if (result < 0)
      LOGE(TAG, "Failed to send mailbox message via ioctl. Error: %s", strerror(errno));

   if (close(mbox) < 0)
      LOGE(TAG, "Failed to close /dev/vcio. Error: %s", strerror(errno));

   return result;
}

/**
  @brief  Allocate a block of memory from the VideoCore

  @param  size Number of bytes to reserve
  @param  alignment Requested alignment of memory
  @param  flags Memory flags to issue with request
  @retval int32_t - Handle for allocated memory block. -1 if error
*/
int32_t mailboxAllocateMemory(uint32_t size, uint32_t alignment, uint32_t flags)
{
   mailbox_message_allocate_memory_request_t request;
   memset(&request, 0, sizeof(mailbox_message_allocate_memory_request_t));

   request.header.length = sizeof(mailbox_message_allocate_memory_request_t);
   request.header.code = 0;

   request.tag.header.identifier = mailbox_tag_id_allocate_memory;
   request.tag.header.length = 12; // TODO Define
   request.tag.header.code = 0;

   request.tag.size = size;
   request.tag.alignment = alignment;
   request.tag.flags = flags;

   request.trailer.end = 0;

   if (mailboxSend(&request) < 0)
      return -1;

   mailbox_message_allocate_memory_response_t* response = (mailbox_message_allocate_memory_response_t*) &request;

   if ((response->header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;

   if ((response->tag.header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;

   return response->tag.handle;
}

/**
  @brief  Lock a pre-allocated block of memory for use

  @param  handle Handle from allocation command
  @retval void* - Bus address of memory block. NULL if error
*/
void* mailboxLockMemory(uint32_t handle)
{
   mailbox_lock_memory_request_t request;
   memset(&request, 0, sizeof(mailbox_lock_memory_request_t));

   request.header.length = sizeof(mailbox_lock_memory_request_t);
   request.header.code = 0;

   request.tag.header.identifier = mailbox_tag_id_lock_memory;
   request.tag.header.length = 4; // TODO Define
   request.tag.header.code = 0;

   request.tag.handle = handle;

   request.trailer.end = 0;

   if (mailboxSend(&request) < 0)
      return NULL;

   mailbox_lock_memory_response_t* response = (mailbox_lock_memory_response_t*) &request;

   if ((response->header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return NULL;
      

   if ((response->tag.header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return NULL;

   return (void*)response->tag.busAddress;
}

/**
  @brief  Unlock a allocated memory block.

  @param  handle Handle from allocation command
  @retval int32_t - Result of command. -1 if error
*/
int32_t mailboxUnlockMemory(uint32_t handle)
{
   mailbox_unlock_memory_request_t request;
   memset(&request, 0, sizeof(mailbox_unlock_memory_request_t));

   request.header.length = sizeof(mailbox_unlock_memory_request_t);
   request.header.code = 0;

   request.tag.header.identifier = mailbox_tag_id_unlock_memory;
   request.tag.header.length = 4; // TODO Define
   request.tag.header.code = 0;

   request.tag.handle = handle;

   request.trailer.end = 0;

   if (mailboxSend(&request) < 0)
      return -1;

   mailbox_unlock_memory_response_t* response = (mailbox_unlock_memory_response_t*) &request;

   if ((response->header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;
      

   if ((response->tag.header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;

   return response->tag.status;
}

/**
  @brief  Release an allocated block of memory back to the VC

  @param  handle Handle from allocation command
  @retval int32_t - Result of command. -1 if error
*/
int32_t mailboxReleaseMemory(uint32_t handle)
{
   mailbox_release_memory_request_t request;
   memset(&request, 0, sizeof(mailbox_release_memory_request_t));

   request.header.length = sizeof(mailbox_release_memory_request_t);
   request.header.code = 0;

   request.tag.header.identifier = mailbox_tag_id_release_memory;
   request.tag.header.length = 4; // TODO Define
   request.tag.header.code = 0;

   request.tag.handle = handle;

   request.trailer.end = 0;

   if (mailboxSend(&request) < 0)
      return -1;

   mailbox_release_memory_response_t* response = (mailbox_release_memory_response_t*) &request;

   if ((response->header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;
      

   if ((response->tag.header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;

   return response->tag.status;
}

/**
  @brief  Fetch the DMA channel mask of in-use channel

  @param  none
  @retval uint32_t - 0 indicates in use, 1 available
*/
uint32_t mailboxGetDmaChannelMask()
{
   mailbox_dma_channel_request_t request;
   memset(&request, 0, sizeof(mailbox_dma_channel_request_t));

   request.header.length = sizeof(mailbox_dma_channel_request_t);
   request.header.code = 0;

   request.tag.header.identifier = mailbox_tag_id_get_dma_channels;
   request.tag.header.length = 4;
   request.tag.header.code = 0;

   request.tag.pad = 0;

   request.trailer.end = 0;

   if (mailboxSend(&request) < 0)
      return -1;

   mailbox_dma_channel_response_t* response = (mailbox_dma_channel_response_t*) &request;

   if ((response->header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;
      

   if ((response->tag.header.code & MAILBOX_CODE_SUCCESS) != MAILBOX_CODE_SUCCESS)
      return -1;

   LOGD(TAG, "DMA Channel Mask: 0x%X", response->tag.mask);

   return response->tag.mask;
}