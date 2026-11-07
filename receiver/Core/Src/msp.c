/**
  ******************************************************************************
  * @file    msp.c
  * @brief   MSP v1 链路实现：50Hz 下行摇杆 + 10Hz 上行姿态
  *
  *  设计要点：
  *   1. 接收走中断 + 环形缓冲 + 状态机，主循环只搬数据，绝不阻塞等待。
  *      阻塞式 HAL_UART_Receive(...,100) 会拖慢 nRF24L01 的轮询，遥控会丢包。
  *   2. 发送用 HAL_UART_Transmit_IT，发完才允许下一帧，不占 CPU。
  *   3. 摇杆帧 20ms 一帧；姿态请求 100ms 一次，且相位比摇杆帧晚 10ms，
  *      两帧不会挤在同一时刻抢同一个串口。
  *   4. 飞控约 50ms 收不到 MSP_SET_RAW_RC 就会 failsafe，所以节奏宁快勿慢。
  ******************************************************************************
  */

#include "msp.h"
#include "main.h"
#include "rc.h"

extern UART_HandleTypeDef huart3;   /* 在 main.c 中定义 */

/* ---- 可调参数 ------------------------------------------------------------ */
/* 0: 遥控器掉线就停发摇杆帧，让飞控自己进 failsafe（推荐，空中更安全）
   1: 掉线也继续发最后一帧的值（仅台架调试用，飞机会一直转） */
#define MSP_SEND_RC             0       /* 0 = sticks go over IBUS now, MSP only reads attitude */
#define MSP_TX_WHEN_RC_LOST     0
#define MSP_RC_PERIOD_MS        20U     /* 摇杆帧周期，50Hz */
#define MSP_ATT_PERIOD_MS       100U    /* 姿态请求周期，10Hz */
#define MSP_ATT_PHASE_MS        10U     /* 与摇杆帧错开的相位 */
#define MSP_ATT_TIMEOUT_MS      500U    /* 超时多久判定姿态失效 */

#define MSP_RX_RING             64U
#define MSP_TX_MAX              (MSP_MAX_PAYLOAD + 6U)

/* ---- 接收缓冲 ------------------------------------------------------------ */
static uint8_t           rx_byte;
static uint8_t           rx_ring[MSP_RX_RING];
static volatile uint16_t rx_head;
static volatile uint16_t rx_tail;
static volatile uint32_t rx_overflow;

/* ---- 发送缓冲 ------------------------------------------------------------ */
static uint8_t           tx_buf[MSP_TX_MAX];
static volatile uint8_t  tx_busy;

/* ---- 解帧状态机 ---------------------------------------------------------- */
typedef enum
{
  MSP_ST_IDLE = 0,
  MSP_ST_M,
  MSP_ST_DIR,
  MSP_ST_SIZE,
  MSP_ST_CMD,
  MSP_ST_PAYLOAD,
  MSP_ST_CRC
} msp_state_t;

static msp_state_t p_state;
static uint8_t     p_dir;
static uint8_t     p_size;
static uint8_t     p_cmd;
static uint8_t     p_idx;
static uint8_t     p_crc;
static uint8_t     p_payload[MSP_MAX_PAYLOAD];

/* ---- 姿态与统计 ---------------------------------------------------------- */
static int16_t          att_roll;       /* 0.1 度 */
static int16_t          att_pitch;      /* 0.1 度 */
static int16_t          att_yaw;        /* 1 度   */
static volatile uint8_t att_valid;
static uint32_t         att_tick;

static uint32_t         stat_tx;
static uint32_t         stat_att;
static uint32_t         stat_err;

#if (MSP_SEND_RC != 0)   /* rc_tick only used when MSP carries the sticks */
static uint32_t         rc_tick;
#endif
static uint32_t         att_req_tick;

/* ========================================================================== */
/*  发送                                                                      */
/* ========================================================================== */

static void MSP_Send(uint8_t cmd, const uint8_t *payload, uint8_t size)
{
  uint8_t i;
  uint8_t n = 0U;
  uint8_t crc;

  if (tx_busy)                    /* 上一帧还没发完，丢掉这一帧 */
  {
    return;
  }
  if (size > MSP_MAX_PAYLOAD)
  {
    return;
  }

  tx_buf[n++] = '$';
  tx_buf[n++] = 'M';
  tx_buf[n++] = '<';
  tx_buf[n++] = size;
  tx_buf[n++] = cmd;

  crc = (uint8_t)(size ^ cmd);
  for (i = 0U; i < size; i++)
  {
    tx_buf[n++] = payload[i];
    crc ^= payload[i];
  }
  tx_buf[n++] = crc;

  tx_busy = 1U;
  if (HAL_UART_Transmit_IT(&huart3, tx_buf, (uint16_t)n) != HAL_OK)
  {
    tx_busy = 0U;
  }
}

/* 把 Rc 里的摇杆值按 AETR 顺序打包成 MSP_SET_RAW_RC */
static void MSP_SendRawRC(void)
{
  uint8_t p[2U * MSP_RC_CHANNELS];
  int16_t ch[MSP_RC_CHANNELS];
  uint8_t i;
  int16_t v;

  ch[0] = Rc.rol;                 /* ch1 roll     */
  ch[1] = Rc.pit;                 /* ch2 pitch    */
  ch[2] = Rc.thr;                 /* ch3 throttle */
  ch[3] = Rc.yaw;                 /* ch4 yaw      */
  ch[4] = Rc.aux[0];              /* ch5  AUX1    */
  ch[5] = Rc.aux[1];              /* ch6  AUX2    */
  ch[6] = Rc.aux[2];              /* ch7  AUX3    */
  ch[7] = Rc.aux[3];              /* ch8  AUX4（aux[4] 是电压，不用） */

  for (i = 0U; i < MSP_RC_CHANNELS; i++)
  {
    v = ch[i];
    if (v < 1000) { v = 1000; }   /* 飞控只认 1000~2000 */
    if (v > 2000) { v = 2000; }
    p[2U * i]      = (uint8_t)((uint16_t)v & 0x00FFU);
    p[2U * i + 1U] = (uint8_t)(((uint16_t)v >> 8) & 0x00FFU);
  }

  MSP_Send((uint8_t)MSP_CMD_SET_RAW_RC, p, (uint8_t)sizeof(p));
  stat_tx++;
}

/* ========================================================================== */
/*  接收：状态机                                                              */
/* ========================================================================== */

static void MSP_HandleFrame(void)
{
  if (p_dir != '>')               /* 只关心飞控发回来的帧 */
  {
    return;
  }

  if ((p_cmd == (uint8_t)MSP_CMD_ATTITUDE) && (p_size >= 6U))
  {
    att_roll  = (int16_t)((uint16_t)p_payload[0] | ((uint16_t)p_payload[1] << 8));
    att_pitch = (int16_t)((uint16_t)p_payload[2] | ((uint16_t)p_payload[3] << 8));
    att_yaw   = (int16_t)((uint16_t)p_payload[4] | ((uint16_t)p_payload[5] << 8));
    att_tick  = HAL_GetTick();
    att_valid = 1U;
    stat_att++;
  }
}

static void MSP_ParseByte(uint8_t b)
{
  switch (p_state)
  {
    case MSP_ST_IDLE:
      if (b == (uint8_t)'$') { p_state = MSP_ST_M; }
      break;

    case MSP_ST_M:
      p_state = (b == (uint8_t)'M') ? MSP_ST_DIR : MSP_ST_IDLE;
      break;

    case MSP_ST_DIR:
      if ((b == (uint8_t)'>') || (b == (uint8_t)'<'))
      {
        p_dir   = b;
        p_state = MSP_ST_SIZE;
      }
      else
      {
        p_state = MSP_ST_IDLE;
      }
      break;

    case MSP_ST_SIZE:
      p_size = b;
      if (p_size > MSP_MAX_PAYLOAD)
      {
        p_state = MSP_ST_IDLE;
        stat_err++;
      }
      else
      {
        p_state = MSP_ST_CMD;
      }
      break;

    case MSP_ST_CMD:
      p_cmd   = b;
      p_crc   = (uint8_t)(p_size ^ p_cmd);
      p_idx   = 0U;
      p_state = (p_size == 0U) ? MSP_ST_CRC : MSP_ST_PAYLOAD;
      break;

    case MSP_ST_PAYLOAD:
      p_payload[p_idx++] = b;
      p_crc ^= b;
      if (p_idx >= p_size) { p_state = MSP_ST_CRC; }
      break;

    case MSP_ST_CRC:
      p_state = MSP_ST_IDLE;
      if (b == p_crc) { MSP_HandleFrame(); }
      else            { stat_err++; }
      break;

    default:
      p_state = MSP_ST_IDLE;
      break;
  }
}

/* ========================================================================== */
/*  对外接口                                                                  */
/* ========================================================================== */

void MSP_Init(void)
{
  rx_head      = 0U;
  rx_tail      = 0U;
  rx_overflow  = 0U;
  tx_busy      = 0U;
  p_state      = MSP_ST_IDLE;
  att_valid    = 0U;
  stat_tx      = 0U;
  stat_att     = 0U;
  stat_err     = 0U;
  att_roll     = 0;
  att_pitch    = 0;
  att_yaw      = 0;

#if (MSP_SEND_RC != 0)
  rc_tick      = HAL_GetTick();
#endif
  /* 姿态请求比摇杆帧晚 10ms 出发，两个定时器各走各的，不会抢串口 */
  att_req_tick = HAL_GetTick() - (MSP_ATT_PERIOD_MS - MSP_ATT_PHASE_MS);

  (void)HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);
}

void MSP_Task(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t  b;

  /* ---- 1. 把环形缓冲里的字节喂给状态机 ---- */
  while (rx_tail != rx_head)
  {
    b = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) % MSP_RX_RING);
    MSP_ParseByte(b);
  }

  /* ---- 2. 50Hz 下发摇杆 ---- */
#if (MSP_SEND_RC != 0)
  if ((now - rc_tick) >= MSP_RC_PERIOD_MS)
  {
    if (!tx_busy)
    {
      rc_tick = now;
#if (MSP_TX_WHEN_RC_LOST != 0)
      MSP_SendRawRC();
#else
      if (RC_IsOnline()) { MSP_SendRawRC(); }
#endif
    }
  }
#else
  (void)MSP_SendRawRC;   /* sticks go over IBUS; keep function referenced */
#endif

  /* ---- 3. 10Hz 请求姿态 ---- */
  if ((now - att_req_tick) >= MSP_ATT_PERIOD_MS)
  {
    if (!tx_busy)
    {
      att_req_tick = now;
      MSP_Send((uint8_t)MSP_CMD_ATTITUDE, 0, 0U);
    }
  }

  /* ---- 4. 姿态超时判定 ---- */
  if (att_valid && ((now - att_tick) > MSP_ATT_TIMEOUT_MS))
  {
    att_valid = 0U;
  }
}

uint8_t MSP_AttValid(void)
{
  return att_valid;
}

void MSP_GetAttitude(int16_t *roll_01deg, int16_t *pitch_01deg, int16_t *yaw_deg)
{
  if (roll_01deg  != 0) { *roll_01deg  = att_roll;  }
  if (pitch_01deg != 0) { *pitch_01deg = att_pitch; }
  if (yaw_deg     != 0) { *yaw_deg     = att_yaw;   }
}

uint32_t MSP_GetTxFrames(void)  { return stat_tx;  }
uint32_t MSP_GetAttFrames(void) { return stat_att; }
uint32_t MSP_GetErrFrames(void) { return stat_err; }

/* ========================================================================== */
/*  HAL 回调：只能在这里重新挂接收，不能在主循环里抢                        */
/* ========================================================================== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    uint16_t next = (uint16_t)((rx_head + 1U) % MSP_RX_RING);

    if (next != rx_tail)
    {
      rx_ring[rx_head] = rx_byte;
      rx_head = next;
    }
    else
    {
      rx_overflow++;              /* 来不及解析，丢字节，计数留证 */
    }

    (void)HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    tx_busy = 0U;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART3)
  {
    /* 溢出/帧错/噪声：清标志并把接收重新挂上，否则串口就哑了 */
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);
    tx_busy = 0U;
    (void)HAL_UART_Receive_IT(&huart3, &rx_byte, 1U);
  }
}
