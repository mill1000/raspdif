#include <assert.h>
#include <stddef.h>

#include "bcm283x.h"
#include "bcm283x_pcm.h"
#include "log.h"

static bcm283x_pcm_t* pcm = NULL;

/**
  @brief  Initialize the PCM object at the given base address

  @param  base Base address of PCM peripheral
  @retval none
*/
void bcm283x_pcm_init(void* base)
{
  assert(base != NULL);
  assert(pcm == NULL);

  pcm = base;
}

/**
  @brief  Use the PCM sync bit to ensure at least 2 PCM clocks have passed

  @param  none
  @retval none
*/
static void bcm283x_pcm_sync()
{
  assert(pcm != NULL);

  WMB();

  // SYNC bit takes 2 PCM clocks for written value to be echoed back
  // We don't necessarily know the value of SYNC so toggle it to ensure at least
  // 2 PCM clocks of delay
  pcm->CS_A.SYNC = 0;
  while (pcm->CS_A.SYNC != 0) {}

  pcm->CS_A.SYNC = 1;
  while (pcm->CS_A.SYNC != 1) {}

  RMB();
}

/**
  @brief  Reset the PCM peripheral

  @param  none
  @retval none
*/
void bcm283x_pcm_reset()
{
  assert(pcm != NULL);

  WMB();

  // No true reset in block, set registers according to datasheet

  // Disable entire block
  pcm->CS_A.EN = 0;

  bcm283x_delay_microseconds(10);

  // Set entire regster to 0
  pcm->CS_A = (pcm_control_status_t){0};

  // Clear FIFO
  pcm->CS_A.TXCLR = 1;
  pcm->CS_A.RXCLR = 1;

  // Clear error flags
  pcm->CS_A.TXERR = 1;
  pcm->CS_A.RXERR = 1;

  // Reset mode register
  pcm->MODE_A = (pcm_mode_t){0};

  // Reset channel registers
  pcm->RXC_A = (pcm_rx_config_t){0};
  pcm->TXC_A = (pcm_tx_config_t){0};

  // Reset DMA register
  pcm->DREQ_A.TX_PANIC = 0x10;
  pcm->DREQ_A.RX_PANIC = 0x30;
  pcm->DREQ_A.TX = 0x30;
  pcm->DREQ_A.RX = 0x20;

  // Reset interrupt registers
  pcm->INTEN_A = (pcm_interrupt_enable_t){0};
  pcm->INTSTC_A = (pcm_interrupt_status_t){0};

  // Reset GRAY
  pcm->GRAY = (pcm_gray_control_t){0};
}

/**
  @brief  Clear the PCM FIFOs

  @param  none
  @retval none
*/
void bcm283x_pcm_clear_fifos()
{
  assert(pcm != NULL);

  WMB();

  pcm->CS_A.TXCLR = 1;
  pcm->CS_A.RXCLR = 1;

  bcm283x_pcm_sync();
}

/**
  @brief  Configure the PCM DMA settings

  @param  enable Enable DMA requests from the PCM peripheral
  @param  config PCM DMA configuration for TX & RX
  @retval none
*/
void bcm283x_pcm_configure_dma(bool enable, const pcm_dma_config_t* config)
{
  assert(pcm != NULL);
  assert(config != NULL);

  // Ensure values are within bounds of FIFO
  assert(config->tx_threshold <= 64);
  assert(config->rx_threshold <= 64);
  assert(config->tx_panic <= 64);
  assert(config->rx_panic <= 64);

  WMB();

  pcm->CS_A.DMAEN = enable;

  pcm->DREQ_A.TX_PANIC = config->tx_panic;
  pcm->DREQ_A.TX = config->tx_threshold;

  pcm->DREQ_A.RX_PANIC = config->rx_panic;
  pcm->DREQ_A.RX = config->rx_threshold;
}

/**
  @brief  Configure PCM channels in the target register

  @param  reg Channel configuration register
  @param  channel1 Channel 1 configuration. NULL to disable.
  @param  channel2 Channel 2 configuration. NULL to disable.
  @retval void
*/
static void bcm283x_pcm_configure_channels(volatile pcm_tx_rx_config_t* reg, const pcm_channel_config_t* channel1, const pcm_channel_config_t* channel2)
{
  WMB();

  reg->CH1EN = (channel1 != NULL);
  if (channel1 != NULL)
  {
    reg->CH1POS = channel1->position;
    reg->CH1WID = (channel1->width - 8) & 0xF;
    reg->CH1WEX = channel1->width >= 24;
  }

  reg->CH2EN = (channel2 != NULL);
  if (channel2 != NULL)
  {
    reg->CH2POS = channel2->position;
    reg->CH2WID = (channel2->width - 8) & 0xF;
    reg->CH2WEX = channel2->width >= 24;
  }
}

/**
  @brief  Configure PCM transmit channels

  @param  channel1 Channel 1 configuration. NULL to disable.
  @param  channel2 Channel 2 configuration. NULL to disable.
  @retval void
*/
void bcm283x_pcm_configure_transmit_channels(const pcm_channel_config_t* channel1, const pcm_channel_config_t* channel2)
{
  assert(pcm != NULL);

  bcm283x_pcm_configure_channels(&pcm->TXC_A, channel1, channel2);
}

/**
  @brief  Configure PCM receive channels

  @param  channel1 Channel 1 configuration. NULL to disable.
  @param  channel2 Channel 2 configuration. NULL to disable.
  @retval void
*/
void bcm283x_pcm_configure_receive_channels(const pcm_channel_config_t* channel1, const pcm_channel_config_t* channel2)
{
  assert(pcm != NULL);

  bcm283x_pcm_configure_channels(&pcm->RXC_A, channel1, channel2);
}

/**
  @brief  Configure the PCM mode register according to the provided configuration

  @param  config PCM configuration to set
  @retval void
*/
static void bcm283x_pcm_configure_mode(const pcm_configuration_t* config)
{
  volatile pcm_mode_t* mode = &pcm->MODE_A;

  // Set frame length
  mode->FLEN = config->frame.length - 1;

  // Configure frame sync
  mode->FSLEN = config->frame_sync.length;
  mode->FSI = config->frame_sync.invert;
  mode->FSM = (config->frame_sync.mode == pcm_frame_sync_master) ? 0 : 1;

  // Configure clock
  mode->CLKI = config->clock.invert;
  mode->CLKM = (config->clock.mode == pcm_clock_master) ? 0 : 1;

  // Configure frame format
  mode->FTXP = (config->frame.tx_mode == pcm_frame_unpacked) ? 0 : 1;
  mode->FTXP = (config->frame.rx_mode == pcm_frame_unpacked) ? 0 : 1;

  // Disable PDM mode
  mode->PDME = 0;
  mode->PDMN = 0;

  // Enable PCM clock
  mode->CLK_DIS = 0;
}

/**
  @brief  Configure PCM peripheral

  @param  config PCM configuration to set
  @retval void
*/
void bcm283x_pcm_configure(const pcm_configuration_t* config)
{
  assert(pcm != NULL);
  assert(config != NULL);

  WMB();

  // Enable clock to block
  pcm->CS_A.EN = 1;

  // Disable standby if implemented
  pcm->CS_A.STBY = 1;

  // Make block inactive during config
  pcm->CS_A.TXON = 0;
  pcm->CS_A.RXON = 0;

  bcm283x_delay_microseconds(10);

  // Configure the mode register
  bcm283x_pcm_configure_mode(config);

  // Configure FIFO thresholds for setting TXW and RXW bits
  pcm->CS_A.TXTHR = config->fifo.tx_threshold;
  pcm->CS_A.RXTHR = config->fifo.rx_threshold;

  RMB();

  bcm283x_delay_microseconds(10);
}

/**
  @brief  Enable the PCM interface

  @param  transmit Enable the TX interface
  @param  receive Enable the RX interface
  @retval void
*/
void bcm283x_pcm_enable(bool transmit, bool receive)
{
  WMB();

  pcm->CS_A.EN = 1;

  pcm->CS_A.TXON = transmit;
  pcm->CS_A.RXON = receive;
}