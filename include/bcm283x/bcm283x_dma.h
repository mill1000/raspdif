#ifndef __BCM283X_DMA__
#define __BCM283X_DMA__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/**
  @brief  Internal structures, types and constants
*/

#define DMA_BASE_OFFSET       (0x00007000)
#define DMA_CHANNEL_OFFSET    (0x100)
#define DMA_INT_STATUS_OFFSET (0xFE0)
#define DMA_ENABLE_OFFSET     (0xFF0)

typedef struct dma_control_status_t
{
  uint32_t ACTIVE : 1;
  uint32_t END : 1;
  uint32_t INT : 1;
  uint32_t DREQ : 1;
  uint32_t PAUSED : 1;
  uint32_t DREQ_STOPS_DMA : 1;
  uint32_t WAITING_FOR_OUTSTANDING_WRITES : 1;
  uint32_t _reserved : 1;
  uint32_t ERROR : 1;
  uint32_t _reserved2 : 7;
  uint32_t PRIORITY : 4;
  uint32_t PANIC_PRIORITY : 4;
  uint32_t _reserved3 : 4;
  uint32_t WAIT_FOR_OUSTANDING_WRITES : 1;
  uint32_t DISDEBUG : 1;
  uint32_t ABORT : 1;
  uint32_t RESET : 1;
} dma_control_status_t;

typedef struct dma_transfer_information_t
{
  uint32_t INTEN : 1;
  uint32_t TDMODE : 1;
  uint32_t _reserved : 1;
  uint32_t WAIT_RESP : 1;
  uint32_t DEST_INC : 1;
  uint32_t DEST_WIDTH : 1;
  uint32_t DEST_DREQ : 1;
  uint32_t DEST_IGNORE : 1;
  uint32_t SRC_INC : 1;
  uint32_t SRC_WIDTH : 1;
  uint32_t SRC_DREQ : 1;
  uint32_t SRC_IGNORE : 1;
  uint32_t BURST_LENGTH : 4;
  uint32_t PERMAP : 5;
  uint32_t WAITS : 5;
  uint32_t NO_WIDE_BURSTS : 1;
  uint32_t _reserved2 : 5;
} dma_transfer_information_t;

typedef struct dma_lite_transfer_information_t
{
  uint32_t INTEN : 1;
  uint32_t _reserved : 2;
  uint32_t WAIT_RESP : 1;
  uint32_t DEST_INC : 1;
  uint32_t DEST_WIDTH : 1;
  uint32_t DEST_DREQ : 1;
  uint32_t DEST_IGNORE : 1;
  uint32_t SRC_INC : 1;
  uint32_t SRC_WIDTH : 1;
  uint32_t SRC_DREQ : 1;
  uint32_t SRC_IGNORE : 1;
  uint32_t BURST_LENGTH : 4;
  uint32_t PERMAP : 5;
  uint32_t WAITS : 5;
  uint32_t _reserved2 : 6;
} dma_lite_transfer_information_t;

typedef struct dma_transfer_length_t
{
  uint32_t XLENGTH : 16;
  uint32_t YLENGTH : 14;
  uint32_t _reserved : 2;
} dma_transfer_length_t;

typedef struct dma_lite_transfer_length_t
{
  uint32_t XLENGTH : 16;
  uint32_t _reserved : 16;
} dma_lite_transfer_length_t;

typedef struct dma_stride_t
{
  uint32_t S_STRIDE : 16;
  uint32_t D_STRIDE : 16;
} dma_stride_t;

typedef struct dma_debug_t
{
  uint32_t READ_LAST_NOT_SET_ERROR : 1;
  uint32_t FIFO_ERROR : 1;
  uint32_t READ_ERROR : 1;
  uint32_t _reserved : 1;
  uint32_t OUTSTANDING_WRITES : 4;
  uint32_t DMA_ID : 8;
  uint32_t DMA_STATE : 8;
  uint32_t VERSION : 3;
  uint32_t LITE : 1;
  uint32_t _reserved2 : 3;
} dma_debug_t;

typedef struct dma_interrupt_status_t
{
  uint32_t INT0 : 1;
  uint32_t INT1 : 1;
  uint32_t INT2 : 1;
  uint32_t INT3 : 1;
  uint32_t INT4 : 1;
  uint32_t INT5 : 1;
  uint32_t INT6 : 1;
  uint32_t INT7 : 1;
  uint32_t INT8 : 1;
  uint32_t INT9 : 1;
  uint32_t INT10 : 1;
  uint32_t INT11 : 1;
  uint32_t INT12 : 1;
  uint32_t INT13 : 1;
  uint32_t INT14 : 1;
  uint32_t INT15 : 1;
  uint32_t _reserved : 16;
} dma_interrupt_status_t;

typedef struct dma_enable_t
{
  uint32_t EN0 : 1;
  uint32_t EN1 : 1;
  uint32_t EN2 : 1;
  uint32_t EN3 : 1;
  uint32_t EN4 : 1;
  uint32_t EN5 : 1;
  uint32_t EN6 : 1;
  uint32_t EN7 : 1;
  uint32_t EN8 : 1;
  uint32_t EN9 : 1;
  uint32_t EN10 : 1;
  uint32_t EN11 : 1;
  uint32_t EN12 : 1;
  uint32_t EN13 : 1;
  uint32_t EN14 : 1;
  uint32_t EN15 : 1;
  uint32_t _reserved : 16;
} dma_enable_t;

typedef enum __attribute__((packed))
{
  DMA_DREQ_ALWAYS_ON    = 0,
  DMA_DREQ_DSI          = 1,
  DMA_DREQ_PCM_TX       = 2,
  DMA_DREQ_PCM_RX       = 3,
  DMA_DREQ_SMI          = 4,
  DMA_DREQ_PWM          = 5,
  DMA_DREQ_SPI_TX       = 6,
  DMA_DREQ_SPI_RX       = 7,
  DMA_DREQ_BSC_SLAVE_TX = 8,
  DMA_DREQ_BSC_SLAVE_RX = 9,
  DMA_DREQ_EMMC         = 11,
  DMA_DREQ_UART_TX      = 12,
  DMA_DREQ_SD_HOST      = 13,
  DMA_DREQ_UART_RX      = 14,
  //DMA_DREQ_DSI_         = 15, // Duplicate?
  DMA_DREQ_SLIMBUX_MCTX = 16,
  DMA_DREQ_HDMI         = 17,
  DMA_DREQ_SLIMBUS_MCRX = 18,
  DMA_DREQ_SLIMBUS_DC0  = 19,
  DMA_DREQ_SLIMBUS_DC1  = 20,
  DMA_DREQ_SLIMBUS_DC2  = 21,
  DMA_DREQ_SLIMBUS_DC3  = 22,
  DMA_DREQ_SLIMBUS_DC4  = 23,
  DMA_DREQ_SCALER_FIFO_0_SMI  = 24,
  DMA_DREQ_SCALER_FIFO_1_SMI  = 25,
  DMA_DREQ_SCALER_FIFO_2_SMI  = 26,
  DMA_DREQ_SLIMBUS_DC5  = 27,
  DMA_DREQ_SLIMBUS_DC6  = 28,
  DMA_DREQ_SLIMBUS_DC7  = 29,
  DMA_DREQ_SLIMBUS_DC8  = 30,
  DMA_DREQ_SLIMBUS_DC9  = 31,
} DMA_DREQ_SIGNAL;

typedef struct dma_control_block_t dma_control_block_t;
struct dma_control_block_t
{
  dma_transfer_information_t  transferInformation; // TI
  void*                       sourceAddress; // SOURCE_AD
  void*                       destinationAddress; // DEST_AD
  dma_transfer_length_t       transferLength; // TXFR_LEN
  dma_stride_t                stride; // STRIDE
  dma_control_block_t*        nextControlBlock; // NEXTCONBK
  uint32_t                    _reserved[2];
};

typedef struct bcm283x_dma_channel_t
{
  volatile dma_control_status_t         CS;
  volatile dma_control_block_t*         CONBLK_AD;
  volatile dma_transfer_information_t   TI;
  volatile uint32_t                     SOURCE_AD;
  volatile uint32_t                     DEST_AD;
  volatile dma_transfer_length_t        TXFR_LEN;
  volatile dma_stride_t                 STRIDE;
  volatile dma_control_block_t*         NEXTCONBK;
  volatile dma_debug_t                  DEBUG;
} bcm283x_dma_channel_t;

static_assert(sizeof(dma_control_status_t) == sizeof(uint32_t), "dma_control_status_t must be 4 bytes.");
static_assert(sizeof(dma_transfer_information_t) == sizeof(uint32_t), "dma_transfer_information_t must be 4 bytes.");
static_assert(sizeof(dma_transfer_length_t) == sizeof(uint32_t), "dma_transfer_length_t must be 4 bytes.");
static_assert(sizeof(dma_stride_t) == sizeof(uint32_t), "dma_stride_t must be 4 bytes.");
static_assert(sizeof(dma_debug_t) == sizeof(uint32_t), "dma_debug_t must be 4 bytes.");
static_assert(sizeof(dma_interrupt_status_t) == sizeof(uint32_t),"dma_interrupt_status_t must be 4 bytes.");
static_assert(sizeof(dma_enable_t) == sizeof(uint32_t), "dma_enable_t must be 4 bytes.");

static_assert(sizeof(dma_control_block_t) == 8 * sizeof(uint32_t), "dma_control_block_t must be 32 bytes.");

static_assert(sizeof(bcm283x_dma_channel_t) == 9 * sizeof(uint32_t), "bcm283x_dma_channel_t must be 36 bytes.");

/**
  @brief  External structures, types and interfaces.
*/
typedef enum dma_channel_t
{
  dma_channel_0,
  dma_channel_1,
  dma_channel_2,
  dma_channel_3,
  dma_channel_4,
  dma_channel_5,
  dma_channel_6,
  dma_channel_7,
  dma_channel_8,
  dma_channel_9,
  dma_channel_10,
  dma_channel_11,
  dma_channel_12,
  dma_channel_13,
  dma_channel_14,
  // Sorry channel 15, you're weird
  dma_channel_max,
} dma_channel_t;

void dmaInit(void* base);
void dmaReset(dma_channel_t channel);
void dmaSetControlBlock(dma_channel_t channel, const dma_control_block_t* control);
void dmaEnable(dma_channel_t channel, bool enable);
bool dmaActive(dma_channel_t channel);
#endif