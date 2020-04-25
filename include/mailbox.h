#ifndef __MAILBOX__
#define __MAILBOX__

#include "bcm283x_mailbox.h"

#define VCIO_IOC_MAGIC      100
#define IOCTL_MBOX_PROPERTY _IOWR(VCIO_IOC_MAGIC, 0, char *)

int32_t mailboxAllocateMemory(uint32_t size, uint32_t alignment, uint32_t flags);
void* mailboxLockMemory(uint32_t handle);
int32_t mailboxUnlockMemory(uint32_t handle);
int32_t mailboxReleaseMemory(uint32_t handle);
uint32_t mailboxGetDmaChannelMask(void);
#endif
