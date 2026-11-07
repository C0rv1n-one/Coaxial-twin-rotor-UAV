/**
  ******************************************************************************
  * @file    rc.c
  * @brief   遥控帧解析 + 信道自动扫描
  ******************************************************************************
  */

#include "rc.h"

RC_Data_t Rc;

#define RC_HEAD0        0xAA
#define RC_HEAD1        0xAF
#define RC_TYPE         0x03
#define RC_MIN_LEN      13          /* 4 字节帧头 + 4 通道 + 校验 */
#define RC_LOST_MS      500         /* 超过该时间没收到帧 -> 掉线 */
#define RC_RESCAN_MS    1000        /* 掉线后多久开始重新扫描信道 */
#define RC_SCAN_DWELL  60         /* 每个信道停留时间 */

static uint8_t  rx_buf[NRF_PAYLOAD_SIZE];
static const uint8_t rc_ack_dummy[4] = {0xFF, 0xFF, 0xFF, 0xFF};  /* 回传内容, 遥控器不解析 */
static uint8_t  rc_locked   = 0;    /* 1: 已锁定在有信号的信道上 */
static uint32_t rc_last_ok  = 0;    /* 最近一次收到有效帧的时刻 */
static uint32_t rc_scan_tick = 0;
static uint8_t  rc_scan_ch  = 0;
static uint8_t  rpd_map[16];
static uint8_t  rpd_hits    = 0;
static uint8_t  rpd_last    = 0;
static uint8_t  payload_fixed = 0;
static uint32_t rpd_tick    = 0;

/**
  * @brief  解析 ANO 遥控帧
  * @retval 1: 校验通过并已更新 Rc
  */
uint8_t RC_Parse(const uint8_t *buf, uint8_t len)
{
  uint8_t  sum = 0;
  uint8_t  i;
  uint8_t  n;
  int16_t  v[10];

  if ((len < RC_MIN_LEN) || (len > NRF_PAYLOAD_SIZE))
  {
    return 0;
  }
  if ((buf[0] != RC_HEAD0) || (buf[1] != RC_HEAD1) || (buf[2] != RC_TYPE))
  {
    return 0;
  }
  if (buf[3] != (uint8_t)(len - 5))          /* ANO 帧: [3] = 数据区字节数(不含 4 字节帧头和 1 字节校验) */
  {
    return 0;
  }
  if (((len - 5) & 0x01) != 0)              /* 数据区必须是偶数个字节 */
  {
    return 0;
  }

  for (i = 0; i < (uint8_t)(len - 1); i++)
  {
    sum = (uint8_t)(sum + buf[i]);
  }
  if (sum != buf[len - 1])
  {
    return 0;
  }

  n = (uint8_t)((len - 5) / 2);             /* 有效通道个数 */
  if (n < 4)
  {
    return 0;
  }
  if (n > 10)
  {
    n = 10;
  }

  for (i = 0; i < n; i++)
  {
    v[i] = (int16_t)(((uint16_t)buf[4 + 2 * i] << 8) | (uint16_t)buf[5 + 2 * i]);
  }

  Rc.thr = v[0];
  Rc.yaw = v[1];
  Rc.rol = v[2];
  Rc.pit = v[3];
  for (i = 0; i < 6; i++)
  {
    Rc.aux[i] = (i < (uint8_t)(n - 4)) ? v[4 + i] : 0;
  }

  return 1;
}

/**
  * @brief  清状态 (初始化时调用一次)
  */
void RC_Init(void)
{
  uint8_t i;

  Rc.thr = 1500;
  Rc.yaw = 1500;
  Rc.rol = 1500;
  Rc.pit = 1500;
  for (i = 0; i < 6; i++)
  {
    Rc.aux[i] = 0;
  }
  Rc.channel = NRF24L01_GetChannel();
  Rc.online  = 0;
  Rc.packets = 0;
  Rc.rf_packets = 0;

  rc_locked    = 0;
  rc_scan_ch   = NRF24L01_GetChannel();
  rc_last_ok   = 0;
  rc_scan_tick = 0;
  rpd_hits     = 0;
  rpd_last     = 0;
  rpd_tick     = 0;
  payload_fixed = 0;
  NRF24L01_SetPayloadMode(0);
  {
    uint8_t i;
    for (i = 0; i < 16; i++) rpd_map[i] = 0;
  }
}

uint8_t RC_IsOnline(void)
{
  if (Rc.packets == 0)
  {
    return 0;
  }
  return ((HAL_GetTick() - rc_last_ok) < RC_LOST_MS) ? 1 : 0;
}

/**
  * @brief  主循环里反复调用: 收包 -> 解帧 -> 掉线后自动扫描信道
  */
uint8_t RC_GetPayloadMode(void)
{
  return payload_fixed;
}


void RC_Task(void)
{
  uint8_t len = 0;

  if (NRF24L01_ReadPacket(rx_buf, &len))
  {
    Rc.rf_packets++;
    /* 这一包的 ACK 已由硬件发出; 立刻装下一包的 ACK 数据,
       遥控器收到就会把蓝灯点亮并在屏幕上显示“已连接” */
    NRF24L01_LoadAckPayload(rc_ack_dummy, sizeof(rc_ack_dummy));

    if (RC_Parse(rx_buf, len))
    {
      rc_last_ok = HAL_GetTick();
      Rc.packets++;
      Rc.online  = 1;
      Rc.channel = NRF24L01_GetChannel();
      rc_locked  = 1;
      rc_scan_ch = Rc.channel;
    }
  }

  /* 掉线: 一段时间收不到有效帧就开始重新扫信道 */
  if (rc_locked && ((HAL_GetTick() - rc_last_ok) > RC_RESCAN_MS))
  {
    rc_locked    = 0;
    Rc.online    = 0;
    rc_scan_ch   = NRF24L01_GetChannel();
    rc_scan_tick = HAL_GetTick();
  }

  /* RPD: sample every 5ms, mark channel if carrier above -64dBm */
  if (!rc_locked && ((HAL_GetTick() - rpd_tick) >= 5U))
  {
    uint8_t ch;
    rpd_tick = HAL_GetTick();
    ch = NRF24L01_GetChannel();
    if (NRF24L01_GetRPD())
    {
      uint8_t m = (uint8_t)(1U << (ch & 7U));
      if ((rpd_map[ch >> 3] & m) == 0)
      {
        rpd_map[ch >> 3] |= m;
        rpd_hits++;
      }
      rpd_last = ch;
    }
  }
  if (!rc_locked && ((HAL_GetTick() - rc_scan_tick) > RC_SCAN_DWELL))
  {
    rc_scan_tick = HAL_GetTick();

    rc_scan_ch++;
    if (rc_scan_ch > 127)              /* 射频信道 0..127, 遥控器保存的信道可能落在 126/127 */
    {
      rc_scan_ch = 0;
      payload_fixed ^= 1;                          /* toggle payload mode after a full sweep */
      NRF24L01_SetPayloadMode(payload_fixed);
    }
    NRF24L01_SetChannel(rc_scan_ch);
    Rc.channel = rc_scan_ch;
  }
}

/**
  * @brief  RPD ??: ??????????????, ?????????
  */
void RC_GetRpd(uint8_t *last, uint8_t *hits)
{
  if (last) *last = rpd_last;
  if (hits) *hits = rpd_hits;
}
