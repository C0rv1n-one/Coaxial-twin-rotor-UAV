/**
  ******************************************************************************
  * @file    nrf24l01.c
  * @brief   NRF24L01 接收驱动（HAL / SPI1），参数对齐 ANO Remoter 遥控�?  ******************************************************************************
  */

#include "nrf24l01.h"

extern SPI_HandleTypeDef hspi1;

/* ------------------------------ 引脚操作 ------------------------------ */
#define NRF_CSN_LOW()   HAL_GPIO_WritePin(CSN_GPIO_Port, CSN_Pin, GPIO_PIN_RESET)
#define NRF_CSN_HIGH()  HAL_GPIO_WritePin(CSN_GPIO_Port, CSN_Pin, GPIO_PIN_SET)
#define NRF_CE_LOW()    HAL_GPIO_WritePin(CE_GPIO_Port, CE_Pin, GPIO_PIN_RESET)
#define NRF_CE_HIGH()   HAL_GPIO_WritePin(CE_GPIO_Port, CE_Pin, GPIO_PIN_SET)

/* ANO Remoter 3.6 固定使用�?5 字节地址 */
static const uint8_t nrf_addr[5] = {0xAA, 0xBB, 0xCC, 0x00, 0x01};

static uint8_t nrf_channel = 0;      /* 当前信道 */
static uint16_t nrf_rx_events = 0;   /* RX_DR ??????? */
static uint16_t nrf_rx_bad   = 0;    /* RX_DR ??????? */
static uint8_t  nrf_last_status = 0;
static uint8_t  nrf_mode_tx = 0;      /* 1: TX beacon mode, 0: RX mode */   /* RX_DR set counter, regardless of payload */

/* ------------------------------ 底层 SPI ------------------------------ */

/**
  * @brief  SPI 收发一个字�?(模式 0, MSB 先行)
  */
static uint8_t NRF_SPI_RW(uint8_t tx)
{
  uint8_t rx = 0xFF;
  (void)HAL_SPI_TransmitReceive(&hspi1, &tx, &rx, 1, 20);
  return rx;
}

/**
  * @brief  写寄存器, 返回 STATUS
  */
static uint8_t NRF_WriteReg(uint8_t reg, uint8_t value)
{
  uint8_t status;
  NRF_CSN_LOW();
  status = NRF_SPI_RW(reg);
  NRF_SPI_RW(value);
  NRF_CSN_HIGH();
  return status;
}

/**
  * @brief  读寄存器
  */
static uint8_t NRF_ReadReg(uint8_t reg)
{
  uint8_t value;
  NRF_CSN_LOW();
  NRF_SPI_RW(reg);
  value = NRF_SPI_RW(0xFF);
  NRF_CSN_HIGH();
  return value;
}

/**
  * @brief  连续�?(reg 里已�?W_REGISTER �?
  */
static void NRF_WriteBuf(uint8_t reg, const uint8_t *buf, uint8_t len)
{
  NRF_CSN_LOW();
  NRF_SPI_RW(reg);
  while (len--)
  {
    NRF_SPI_RW(*buf++);
  }
  NRF_CSN_HIGH();
}

/**
  * @brief  连续�?(reg 里已�?R_REGISTER �?
  */
static void NRF_ReadBuf(uint8_t reg, uint8_t *buf, uint8_t len)
{
  NRF_CSN_LOW();
  NRF_SPI_RW(reg);
  while (len--)
  {
    *buf++ = NRF_SPI_RW(0xFF);
  }
  NRF_CSN_HIGH();
}

/* ------------------------------ 对外接口 ------------------------------ */

/**
  * @brief  检查模块是否在�? �?TX_ADDR 再读回比�?  * @retval 1: 正常, 0: 失败
  */
uint8_t NRF24L01_Check(void)
{
  uint8_t buf[5];
  uint8_t i;

  NRF_WriteBuf(NRF_W_REGISTER + NRF_TX_ADDR, nrf_addr, 5);
  NRF_ReadBuf(NRF_R_REGISTER + NRF_TX_ADDR, buf, 5);

  for (i = 0; i < 5; i++)
  {
    if (buf[i] != nrf_addr[i])
    {
      return 0;
    }
  }
  return 1;
}

/**
  * @brief  切换接收信道 (CE 拉低时才能改 RF_CH)
  */
void NRF24L01_SetChannel(uint8_t channel)
{
  if (channel > 125)
  {
    channel = 125;
  }

  NRF_CE_LOW();
  NRF_WriteReg(NRF_W_REGISTER + NRF_RF_CH, channel);
  NRF_WriteReg(NRF_FLUSH_RX, 0xFF);
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x70);
  nrf_channel = channel;
  NRF_CE_HIGH();
}

uint8_t NRF24L01_GetChannel(void)
{
  return nrf_channel;
}

/**
  * @brief  初始化为接收�?  * @param  channel: 起始信道 (遥控器默�?0)
  */
void NRF24L01_Init(uint8_t channel)
{
  NRF_CE_LOW();
  HAL_Delay(10);                                  /* 等模块上电稳�?*/

  /* 地址: 收发都用同一地址, 便于自动 ACK */
  NRF_WriteBuf(NRF_W_REGISTER + NRF_RX_ADDR_P0, nrf_addr, 5);
  NRF_WriteBuf(NRF_W_REGISTER + NRF_TX_ADDR, nrf_addr, 5);

  /* �?ANO_Drv_Nrf24l01.c 完全一致的参数 */
  NRF_WriteReg(NRF_W_REGISTER + NRF_EN_AA,       0x01);   /* 通道0 自动应答 */
  NRF_WriteReg(NRF_W_REGISTER + NRF_EN_RXADDR,   0x01);   /* 只开通道0 */
  NRF_WriteReg(NRF_W_REGISTER + NRF_SETUP_AW,    0x03);   /* 5 字节地址 */
  NRF_WriteReg(NRF_W_REGISTER + NRF_SETUP_RETR,  0x1A);   /* 500us, 重发 10 �?*/
  NRF_WriteReg(NRF_W_REGISTER + NRF_RF_SETUP,    0x0F);   /* 2 Mbps, 0 dBm */
  NRF_WriteReg(NRF_W_REGISTER + NRF_RX_PW_P0,    NRF_PAYLOAD_SIZE);

  /* 动态载�?(遥控�?TX2 模式在发数据时就是这么配�? */
  /* �� ANO ����һ��: �ȷ� ACTIVATE ����, ���� SI24R1 / �Ͽ�ģ�� */
  NRF_CSN_LOW();
  NRF_SPI_RW(0x50);
  NRF_SPI_RW(0x73);
  NRF_CSN_HIGH();

  NRF_WriteReg(NRF_W_REGISTER + NRF_DYNPD,       0x01);
  NRF_WriteReg(NRF_W_REGISTER + NRF_FEATURE,
               NRF_FEATURE_EN_DPL | NRF_FEATURE_EN_ACK_PAY);

  /* ?????????????: ?? DYNPD, ??????? 25 ?? */
  if ((NRF_ReadReg(NRF_R_REGISTER | NRF_DYNPD) & 0x01U) == 0U)
  {
    NRF_WriteReg(NRF_W_REGISTER + NRF_RX_PW_P0, 25);   /* fallback to fixed 25 bytes */
  }

  nrf_channel = (channel > 125) ? 125 : channel;
  NRF_CE_LOW();
  NRF_WriteReg(NRF_W_REGISTER + NRF_RF_CH, nrf_channel);

  /* 接收模式: 16 �?CRC + 上电 + PRIM_RX */
  NRF_WriteReg(NRF_W_REGISTER + NRF_CONFIG,
               NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO |
               NRF_CONFIG_PWR_UP | NRF_CONFIG_PRIM_RX);

  NRF_WriteReg(NRF_FLUSH_RX, 0xFF);
  NRF_WriteReg(NRF_FLUSH_TX, 0xFF);
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x70);

  HAL_Delay(2);                                   /* PWR_UP �?1.5ms 才能�?*/
  NRF_CE_HIGH();                                  /* 开始接�?*/


  /* preload 1st ACK payload: handset gets data on its very first ACK */
  {
    uint8_t dummy[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    NRF24L01_LoadAckPayload(dummy, 4);
  }
}

/**
  * @brief  查询并读取一包数�?  * @param  buf: 数据缓冲�?(至少 32 字节)
  * @param  len: 返回实际长度
  * @retval 1: 读到一�? 0: 没有数据
  */
uint8_t NRF24L01_ReadPacket(uint8_t *buf, uint8_t *len)
{
  uint8_t status;
  uint8_t width;

  *len = 0;

  status = NRF_ReadReg(NRF_STATUS);
  nrf_last_status = status;

  if ((status & NRF_STATUS_RX_DR) == 0)
  {
    /* 没数�? 顺手清一�?MAX_RT/TX_DS, 避免 IRQ 一直被拉低 */
    if (status & (NRF_STATUS_MAX_RT | NRF_STATUS_TX_DS))
    {
      NRF_WriteReg(NRF_FLUSH_TX, 0xFF);
      NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x70);
    }
    return 0;
  }

  /* 动态载�? 先读长度 */
  nrf_rx_events++;

  width = NRF_ReadReg(NRF_R_RX_PL_WID);
  if ((width == 0) || (width > NRF_PAYLOAD_SIZE))
  {
    nrf_rx_bad++;
    NRF_WriteReg(NRF_FLUSH_RX, 0xFF);
  }
  else
  {
    NRF_ReadBuf(NRF_R_RX_PAYLOAD, buf, width);
    *len = width;
  }

  /* �?RX_DR, 允许下一次接�?*/
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, NRF_STATUS_RX_DR);

  return (*len > 0) ? 1 : 0;
}

/**
  * @brief  给下一�?ACK 装载回传数据
  * @note   遥控器只有收�?ACK 里带的数据包才算“连上飞机�?蓝灯常亮)�?  *         每收到一包就装一�? 长度随数据包长度一起发回去�?  * @param  data: 回传数据
  * @param  len:  长度 (1~32)
  */
void NRF24L01_LoadAckPayload(const uint8_t *data, uint8_t len)
{
  uint8_t i;

  if (len == 0)
  {
    return;
  }
  if (len > NRF_PAYLOAD_SIZE)
  {
    len = NRF_PAYLOAD_SIZE;
  }

  NRF_CSN_LOW();
  NRF_SPI_RW(NRF_W_ACK_PAYLOAD);          /* 0xA8 | pipe0 */
  for (i = 0; i < len; i++)
  {
    NRF_SPI_RW(data[i]);
  }
  NRF_CSN_HIGH();
}

/**
  * @brief  read any register (debug: raw CONFIG shown on OLED)
  */
uint8_t NRF24L01_ReadRegister(uint8_t reg)
{
  return NRF_ReadReg(NRF_R_REGISTER | (reg & 0x1F));
}

/**
  * @brief  read RPD (received power detector) bit
  */
uint8_t NRF24L01_GetRPD(void)
{
  return (uint8_t)(NRF_ReadReg(NRF_R_REGISTER | NRF_RPD) & 0x01);
}

/**
  * @brief  RF ?????: RX_DR ?????, ???????
  */
uint16_t NRF24L01_GetRxEvents(void)
{
  return nrf_rx_events;
}

/**
  * @brief  RX_DR ??????????
  */
uint16_t NRF24L01_GetRxBad(void)
{
  return nrf_rx_bad;
}

/**
  * @brief  STATUS ???????
  */
uint8_t NRF24L01_GetLastStatus(void)
{
  return nrf_last_status;
}

/**
  * @brief  switch RX payload mode: 0 = dynamic (ANO TX2), 1 = fixed 25 bytes
  */
void NRF24L01_SetPayloadMode(uint8_t fixed_mode)
{
  NRF_CE_LOW();

  NRF_CSN_LOW();
  NRF_SPI_RW(0x50);
  NRF_SPI_RW(0x73);
  NRF_CSN_HIGH();

  if (fixed_mode)
  {
    NRF_WriteReg(NRF_W_REGISTER + NRF_DYNPD,   0x00);
    NRF_WriteReg(NRF_W_REGISTER + NRF_FEATURE, 0x00);
    NRF_WriteReg(NRF_W_REGISTER + NRF_RX_PW_P0, 25);
  }
  else
  {
    NRF_WriteReg(NRF_W_REGISTER + NRF_RX_PW_P0, NRF_PAYLOAD_SIZE);
    NRF_WriteReg(NRF_W_REGISTER + NRF_DYNPD,   0x01);
    NRF_WriteReg(NRF_W_REGISTER + NRF_FEATURE,
                 NRF_FEATURE_EN_DPL | NRF_FEATURE_EN_ACK_PAY);
  }

  NRF_WriteReg(NRF_W_REGISTER + NRF_CONFIG,
               NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO |
               NRF_CONFIG_PWR_UP | NRF_CONFIG_PRIM_RX);
  NRF_WriteReg(NRF_FLUSH_RX, 0xFF);
  NRF_WriteReg(NRF_FLUSH_TX, 0xFF);
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x70);
  NRF_CE_HIGH();
}

/* ---------------------- TX beacon (reverse verification) ---------------------- */

void NRF24L01_EnterTx(void)
{
  if (nrf_mode_tx)
  {
    return;
  }
  NRF_CE_LOW();
  NRF_WriteReg(NRF_W_REGISTER + NRF_CONFIG,
               NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO | NRF_CONFIG_PWR_UP);
  NRF_WriteReg(NRF_FLUSH_TX, 0xFF);
  NRF_CE_HIGH();
  nrf_mode_tx = 1;
}

void NRF24L01_EnterRx(void)
{
  if (!nrf_mode_tx)
  {
    return;
  }
  NRF_CE_LOW();
  NRF_WriteReg(NRF_W_REGISTER + NRF_CONFIG,
               NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO |
               NRF_CONFIG_PWR_UP | NRF_CONFIG_PRIM_RX);
  NRF_WriteReg(NRF_FLUSH_RX, 0xFF);
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x70);
  NRF_CE_HIGH();
  nrf_mode_tx = 0;
}

uint8_t NRF24L01_IsTxMode(void)
{
  return nrf_mode_tx;
}

uint8_t NRF24L01_SendPacket(const uint8_t *buf, uint8_t len)
{
  uint8_t status = 0;
  uint32_t t0;

  NRF_CE_LOW();
  NRF_WriteReg(NRF_FLUSH_TX, 0xFF);
  NRF_WriteBuf(NRF_W_TX_PAYLOAD, buf, len);
  NRF_CE_HIGH();

  t0 = HAL_GetTick();
  while ((HAL_GetTick() - t0) < 5U)
  {
    status = NRF_ReadReg(NRF_STATUS);
    if (status & (NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT))
    {
      break;
    }
  }

  NRF_CE_LOW();
  NRF_WriteReg(NRF_W_REGISTER + NRF_STATUS, 0x30);
  return status;
}
