/**
  ******************************************************************************
  * @file    ibus.c
  * @brief   iBUS 遥控帧发送：USART2(PA2) -> F3 飞控 UART2 RX
  *
  *          飞控侧配置（Betaflight CLI）：
  *            feature RX_SERIAL
  *            set serialrx_provider = IBUS
  *            serial 1 64 115200 57600 0 115200
  *
  *          帧格式：0x20 0x40 + 14 通道 x 2 字节(小端) + 2 字节校验 = 32 字节
  ******************************************************************************
  */

#include "main.h"
#include "ibus.h"
#include "rc.h"

#define IBUS_FRAME_LEN      32U
#define IBUS_CHANNELS       14U
#define IBUS_PERIOD_MS      10U     /* 100Hz，与飞控 serial_update_rate_hz 对齐 */

static UART_HandleTypeDef huart2;
static uint8_t  tx_frame[IBUS_FRAME_LEN];
static uint32_t tx_tick;
static uint32_t tx_frames;

/* 通道值限幅到 1000~2000us */
static uint16_t IBUS_Clamp(int16_t us)
{
  if (us < 1000) { return 1000U; }
  if (us > 2000) { return 2000U; }
  return (uint16_t)us;
}

static void IBUS_BuildFrame(void)
{
  int16_t  src[8];
  uint16_t ch[IBUS_CHANNELS];
  uint16_t sum = 0U;
  uint8_t  i;

  /* 飞控侧 map AETR1234：ch1=roll ch2=pitch ch3=throttle ch4=yaw */
  src[0] = Rc.rol;
  src[1] = Rc.pit;
  src[2] = Rc.thr;
  src[3] = Rc.yaw;
  src[4] = Rc.aux[0];
  src[5] = Rc.aux[1];
  src[6] = Rc.aux[2];
  src[7] = Rc.aux[3];          /* aux[4] 是电池电压，不当通道发 */

  for (i = 0U; i < IBUS_CHANNELS; i++)
  {
    ch[i] = (i < 8U) ? IBUS_Clamp(src[i]) : 1000U;
  }

  tx_frame[0] = 0x20U;
  tx_frame[1] = 0x40U;
  for (i = 0U; i < IBUS_CHANNELS; i++)
  {
    tx_frame[2U + i * 2U] = (uint8_t)(ch[i] & 0xFFU);
    tx_frame[3U + i * 2U] = (uint8_t)(ch[i] >> 8);
  }

  /* 校验 = 0xFFFF - 前面 30 字节之和，小端 */
  for (i = 0U; i < 30U; i++)
  {
    sum = (uint16_t)(sum + tx_frame[i]);
  }
  sum = (uint16_t)(0xFFFFU - sum);
  tx_frame[30] = (uint8_t)(sum & 0xFFU);
  tx_frame[31] = (uint8_t)(sum >> 8);
}

void IBUS_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_USART2_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA2 -> USART2_TX。只发不收，PA3 不用配 */
  gpio.Pin   = GPIO_PIN_2;
  gpio.Mode  = GPIO_MODE_AF_PP;
  gpio.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOA, &gpio);

  huart2.Instance          = USART2;
  huart2.Init.BaudRate     = 115200;
  huart2.Init.WordLength   = UART_WORDLENGTH_8B;
  huart2.Init.StopBits     = UART_STOPBITS_1;
  huart2.Init.Parity       = UART_PARITY_NONE;
  huart2.Init.Mode         = UART_MODE_TX;
  huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  (void)HAL_UART_Init(&huart2);

  /* USART2 IRQ must be enabled: HAL_UART_Transmit_IT pushes bytes from the TXE interrupt.
     Without NVIC the first byte is never written and the UART stays busy forever. */
  HAL_NVIC_SetPriority(USART2_IRQn, 4, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);

  tx_tick   = HAL_GetTick();
  tx_frames = 0U;
}

void IBUS_Task(void)
{
  uint32_t now = HAL_GetTick();

  if ((now - tx_tick) < IBUS_PERIOD_MS)      { return; }
  if (huart2.gState != HAL_UART_STATE_READY)
  {
    /* previous frame not finished in time: abort so the next tick can retry */
    if ((now - tx_tick) > 50U) { (void)HAL_UART_Abort(&huart2); }
    return;
  }
  if (!RC_IsOnline())                        { return; }   /* 无线掉线就不发，让飞控走 failsafe */

  tx_tick = now;
  IBUS_BuildFrame();
  if (HAL_UART_Transmit_IT(&huart2, tx_frame, IBUS_FRAME_LEN) == HAL_OK)
  {
    tx_frames++;
  }
}

uint32_t IBUS_GetFrames(void)
{
  return tx_frames;
}

/* USART2 IRQ: TX only, HAL does the byte shuffling */
void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&huart2);
}
