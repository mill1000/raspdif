#ifndef __BCM283X_PCM__
#define __BCM283X_PCM__

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/**
  @brief  Internal structures, types and constants
*/

#define PCM_BASE_OFFSET (0x00203000)

typedef enum __attribute__((packed))
{
  PCM_THRESHOLD_0  = 0,
  PCM_THRESHOLD_1  = 1,
  PCM_THRESHOLD_2  = 2,
  PCM_THRESHOLD_3  = 3,
} PCM_THRESHOLD;

typedef struct pcm_control_status_t
{
  uint32_t EN    : 1;
  uint32_t RXON  : 1;
  uint32_t TXON  : 1;
  uint32_t TXCLR : 1;
  uint32_t RXCLR : 1;
  uint32_t TXTHR : 2;
  uint32_t RXTHR : 2;
  uint32_t DMAEN : 1;
  uint32_t _reserved : 3;
  uint32_t TXSYNC : 1;
  uint32_t RXSYNC : 1;
  uint32_t TXERR  : 1;
  uint32_t RXERR  : 1;
  uint32_t TXW : 1;
  uint32_t RXR : 1;
  uint32_t TXD : 1;
  uint32_t RXD : 1;
  uint32_t TXE : 1;
  uint32_t RXF : 1;
  uint32_t RXSEX : 1;
  uint32_t SYNC  : 1;
  uint32_t STBY  : 1;
  uint32_t _reserved2 : 6;
} pcm_control_status_t;

typedef struct pcm_mode_t
{
  uint32_t FSLEN : 10;
  uint32_t FLEN  : 10;
  uint32_t FSI  : 1;
  uint32_t FSM  : 1;
  uint32_t CLKI : 1;
  uint32_t CLKM : 1;
  uint32_t FTXP : 1;
  uint32_t FRXP : 1;
  uint32_t PDME : 1;
  uint32_t PDMN : 1;
  uint32_t CLK_DIS : 1;
  uint32_t _reserved : 3;
} pcm_mode_t;

typedef struct pcm_tx_rx_config_t
{
  uint32_t CH2WID : 4;
  uint32_t CH2POS : 10;
  uint32_t CH2EN  : 1;
  uint32_t CH2WEX : 1;
  uint32_t CH1WID : 4;
  uint32_t CH1POS : 10;
  uint32_t CH1EN  : 1;
  uint32_t CH1WEX : 1;
} pcm_tx_rx_config_t;

typedef pcm_tx_rx_config_t pcm_tx_config_t;
typedef pcm_tx_rx_config_t pcm_rx_config_t;

typedef struct pcm_dma_request_t
{
  uint32_t RX : 7;
  uint32_t _reserved : 1;
  uint32_t TX : 7;
  uint32_t _reserved2 : 1;
  uint32_t RX_PANIC : 7;
  uint32_t _reserved3 : 1;
  uint32_t TX_PANIC : 7;
  uint32_t _reserved4 : 1;
} pcm_dma_request_t;

typedef struct pcm_interrupt_enable_t
{
  uint32_t TXW   : 1;
  uint32_t RXR   : 1;
  uint32_t TXERR : 1;
  uint32_t RXERR : 1;
  uint32_t _reserved : 28;
} pcm_interrupt_enable_t;

typedef pcm_interrupt_enable_t pcm_interrupt_status_t;

typedef struct pcm_gray_control_t
{
  uint32_t EN    : 1;
  uint32_t CLR   : 1;
  uint32_t FLUSH : 1;
  uint32_t _reserved : 1;
  uint32_t RXLEVEL : 6;
  uint32_t FLUSHED : 6;
  uint32_t RXFIFOLEVEL : 6;
  uint32_t _reserved2 : 10;
} pcm_gray_control_t;

typedef struct bcm283x_pcm_t
{
  volatile pcm_control_status_t   CS_A;
  volatile uint32_t               FIFO_A;
  volatile pcm_mode_t             MODE_A;
  volatile pcm_rx_config_t        RXC_A;
  volatile pcm_tx_config_t        TXC_A;
  volatile pcm_dma_request_t      DREQ_A;
  volatile pcm_interrupt_enable_t INTEN_A;
  volatile pcm_interrupt_status_t INTSTC_A;
  volatile pcm_gray_control_t     GRAY;
} bcm283x_pcm_t;

static_assert(sizeof(pcm_control_status_t) == sizeof(uint32_t), "pcm_control_status_t must be 4 bytes.");
static_assert(sizeof(pcm_mode_t) == sizeof(uint32_t), "pcm_mode_t must be 4 bytes.");
static_assert(sizeof(pcm_rx_config_t) == sizeof(uint32_t), "pcm_rx_config_t must be 4 bytes.");
static_assert(sizeof(pcm_tx_config_t) == sizeof(uint32_t), "pcm_tx_config_t must be 4 bytes.");
static_assert(sizeof(pcm_dma_request_t) == sizeof(uint32_t), "pcm_dma_request_t must be 4 bytes.");
static_assert(sizeof(pcm_interrupt_enable_t) == sizeof(uint32_t), "pcm_interrupt_enable_t must be 4 bytes.");
static_assert(sizeof(pcm_interrupt_status_t) == sizeof(uint32_t), "pcm_interrupt_status_t must be 4 bytes.");
static_assert(sizeof(pcm_gray_control_t) == sizeof(uint32_t), "pcm_gray_control_t must be 4 bytes.");
static_assert(sizeof(bcm283x_pcm_t) == 9 * sizeof(uint32_t), "bcm283x_pcm_t must be 36 bytes.");

/**
  @brief  External structures, types and interfaces.
*/

typedef enum pcm_frame_sync_mode_t
{
  pcm_frame_sync_master,
  pcm_frame_sync_slave,
} pcm_frame_sync_mode_t;

typedef enum pcm_clock_mode_t
{
  pcm_clock_master,
  pcm_clock_slave,
} pcm_clock_mode_t;

typedef enum pcm_frame_mode_t
{
  pcm_frame_unpacked,
  pcm_frame_packed,
} pcm_frame_mode_t;

typedef struct pcm_channel_config_t
{
  uint8_t width;
  uint8_t position;
} pcm_channel_config_t;

typedef struct pcm_dma_config_t
{
  uint8_t txThreshold;
  uint8_t rxThreshold;
  uint8_t txPanic;
  uint8_t rxPanic;
} pcm_dma_config_t;

typedef enum pcm_fifo_threshold_t
{
  // TX thresholds
  pcm_tx_threshold_empty     = PCM_THRESHOLD_0,
  pcm_tx_threshold_one_third = PCM_THRESHOLD_1, // Unknown, assuming 1/4 or 1/3
  pcm_tx_threshold_two_third = PCM_THRESHOLD_2, // Unknown assuming 3/4 or 2/3
  pcm_tx_threshold_full      = PCM_THRESHOLD_3, // Technically full except for 1 sample

  // RX thresholds
  pcm_rx_threshold_empty     = PCM_THRESHOLD_0, // Technically empty except for 1 sample
  pcm_rx_threshold_one_third = PCM_THRESHOLD_1, // Unknown, assuming 1/4 or 1/3
  pcm_rx_threshold_two_third = PCM_THRESHOLD_2, // Unknown assuming 3/4 or 2/3
  pcm_rx_threshold_full      = PCM_THRESHOLD_3,
} pcm_fifo_threshold_t;

typedef struct pcm_configuration_t
{
  // Frame sync control
  struct
  {
    uint16_t              length;
    bool                  invert;
    pcm_frame_sync_mode_t mode;
  } frameSync;
  // Clock control
  struct
  {
    bool             invert;
    pcm_clock_mode_t mode;  
  } clock;
  // Frame format control
  struct
  {
    pcm_frame_mode_t txMode;
    pcm_frame_mode_t rxMode;
    uint16_t         length;
  } frame;
  // FIFO levels
  struct
  {
    pcm_fifo_threshold_t txThreshold;
    pcm_fifo_threshold_t rxThreshold;
  } fifo;
} pcm_configuration_t;

void bcm283x_pcm_init(void* base);
void bcm283x_pcm_reset(void);
void bcm283x_pcm_clear_fifos(void);
void bcm283x_pcm_configure(const pcm_configuration_t* config);
void bcm283x_pcm_configure_transmit_channels(const pcm_channel_config_t* channel1, const pcm_channel_config_t* channel2);
void bcm283x_pcm_configure_dma(bool enable, const pcm_dma_config_t* config);
void bcm283x_pcm_enable(bool transmit, bool receive);
#endif