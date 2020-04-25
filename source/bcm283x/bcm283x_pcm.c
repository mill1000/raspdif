#include <stddef.h>
#include <assert.h>

#include "bcm283x_pcm.h"
#include "bcm283x.h"
#include "log.h"

static bcm283x_pcm_t* pcm = NULL;

/**
  @brief  Initalize the PCM object at the given base address

  @param  base Base address of PCM peripheral
  @retval none
*/
void pcmInit(void* base)
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
static void pcmSync()
{
  assert(pcm != NULL);

  WMB();

  // SYNC bit takes 2 PCM clocks for written value to be echoed back
  // We don't necessarily know the value of SYNC so toggle it to ensure at least
  // 2 PCM clocks of delay
  pcm->CS_A.SYNC = 0;
  while(pcm->CS_A.SYNC != 0);

  pcm->CS_A.SYNC = 1;
  while(pcm->CS_A.SYNC != 1);

  RMB();
}

/**
  @brief  Reset the PCM peripheral

  @param  none
  @retval none
*/
void pcmReset()
{
  assert(pcm != NULL);

  WMB();

  // Disable entire block
  pcm->CS_A.EN = 0;

  // Disable TX and RX
  pcm->CS_A.TXON = 0;
  pcm->CS_A.RXON = 0;

  // Clear error flags
  pcm->CS_A.TXERR = 1;
  pcm->CS_A.RXERR = 1;

  // Clear FIFO
  pcm->CS_A.TXCLR = 1;
  pcm->CS_A.RXCLR = 1;

  // Disable DMA
  pcm->CS_A.DMAEN = 0;

  // Reset thresholds
  pcm->CS_A.TXTHR = 0;
  pcm->CS_A.RXTHR = 0;
}

/**
  @brief  Clear the PCM FIFOs

  @param  none
  @retval none
*/
void pcmClearFifos()
{
  assert(pcm != NULL);

  WMB();

  pcm->CS_A.TXCLR = 1;
  pcm->CS_A.RXCLR = 1;

  pcmSync();
}

void pcmConfigureTx(const pcm_channel_config_t config[2])
{
  assert(pcm != NULL);
  assert(config != NULL);

  WMB();

  pcm->TXC_A.CH1EN = config[0].enable;
  if (config[0].enable)
  {
    pcm->TXC_A.CH1POS = config[0].position;
    pcm->TXC_A.CH1WID = (config[0].width - 8) & 0xF;
    pcm->TXC_A.CH1WEX = config[0].width >= 24;
  }

  pcm->TXC_A.CH2EN = config[1].enable;
  if (config[1].enable)
  {
    pcm->TXC_A.CH2POS = config[1].position;
    pcm->TXC_A.CH2WID = (config[1].width - 8) & 0xF;
    pcm->TXC_A.CH2WEX = config[1].width >= 24;
  }
}

/**
  @brief  Configure PCM peripheral

  @param  config PCM configuration to set
  @retval void
*/
static void pcmConfigureMode(const pcm_configuration_t* config)
{
  volatile pcm_mode_t* mode = &pcm->MODE_A;

  // Set frame length
  mode->FLEN = config->frameLength - 1;

  // Configure frame sync
  mode->FSLEN = config->frameSyncLength;
  mode->FSI = config->frameSyncInvert;
  mode->FSM = (config->frameSyncMode == pcm_frame_sync_master) ? 0 : 1;

  // Configure clock
  mode->CLKI = config->clockInvert;
  mode->CLKM = (config->clockMode == pcm_clock_master) ? 0 : 1;

  // Configure frame format
  mode->FTXP = (config->txFrameMode == pcm_frame_unpacked) ? 0 : 1;
  mode->FTXP = (config->rxFrameMode == pcm_frame_unpacked) ? 0 : 1;

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
void pcmConfigure(const pcm_configuration_t* config)
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

  pcmConfigureMode(config);
  
  pcm->CS_A.TXTHR = 1;

  RMB();

  bcm283x_delay_microseconds(10);
}

bool pcmFifoNeedsWriting()
{
  assert(pcm != NULL);

  bool room = pcm->CS_A.TXW;

  RMB();

  return room;
}

bool pcmFifoFull()
{
  assert(pcm != NULL);

  bool full = !pcm->CS_A.TXD;

  RMB();

  return full;
}

void pcmWrite(uint32_t data)
{
  assert(pcm != NULL);

  WMB();

  while (pcm->CS_A.TXD == false)
    bcm283x_delay_microseconds(1);

  pcm->FIFO_A = data;

  RMB();
}

void pcmEnable(void)
{
  WMB();

  pcm->CS_A.EN = 1;
  pcm->CS_A.TXON = 1;
}

void pcmDump()
{
  LOGI("PCM", "Registers");
  LOGI("PCM", "CS_A\t\t0x%08X", pcm->CS_A);
  LOGI("PCM", "MODE_A\t\t0x%08X", pcm->MODE_A);
  LOGI("PCM", "RXC_A\t\t0x%08X", pcm->RXC_A);
  LOGI("PCM", "TXC_A\t\t0x%08X", pcm->TXC_A);
  LOGI("PCM", "DREQ_A\t\t0x%08X", pcm->DREQ_A);
  LOGI("PCM", "INTEN_A\t\t0x%08X", pcm->INTEN_A);
  LOGI("PCM", "INTSTC_A\t0x%08X", pcm->INTSTC_A);
  LOGI("PCM", "GRAY\t\t0x%08X", pcm->GRAY);

  RMB();
}