/**
  ******************************************************************************
  * @file    rc.h
  * @brief   解析 ANO Remoter 遥控器发来的遥控帧 (AA AF 03 ...)
  *
  *  帧格式 (ANO_DT.c: ANO_DT_Send_RCData, 每 4ms 发一次, 共 25 字节):
  *    [0] 0xAA  [1] 0xAF  [2] 0x03  [3] 长度(=20)
  *    [4..5]   THR   [6..7]   YAW   [8..9]   ROL   [10..11] PIT
  *    [12..13] AUX1  [14..15] AUX2  [16..17] AUX3
  *    [18..19] AUX4  [20..21] AUX5(电池) [22..23] AUX6
  *    [24] 前 24 字节累加和
  *  所有通道均为 int16 大端
  ******************************************************************************
  */

#ifndef __RC_H__
#define __RC_H__

#include <stdint.h>
#include "nrf24l01.h"

typedef struct
{
  int16_t  thr;           /* 油门   1000 ~ 2000 */
  int16_t  yaw;           /* 航向   1000 ~ 2000 */
  int16_t  rol;           /* 横滚   1000 ~ 2000 */
  int16_t  pit;           /* 俯仰   1000 ~ 2000 */
  int16_t  aux[6];        /* 辅助通道, aux[4] 为电池电压(x100) */
  uint8_t  channel;       /* 当前接收信道 */
  uint8_t  online;        /* 1: 500ms 内收到过有效帧 */
  uint32_t rf_packets;    /* RF ?????(????????) */
  uint32_t packets;       /* 累计有效帧数 */
} RC_Data_t;

extern RC_Data_t Rc;

void    RC_Init(void);                     /* 清状态 */
void    RC_Task(void);                     /* 主循环里反复调用: 收包+解帧+信道扫描 */
void    RC_GetRpd(uint8_t *last, uint8_t *hits); /* RPD ?? */
uint8_t RC_IsOnline(void);
uint8_t RC_GetPayloadMode(void);
uint8_t RC_IsBeacon(void);            /* 0=???? 1=??25?? */                 /* 1: 信号正常 */
uint8_t RC_Parse(const uint8_t *buf, uint8_t len); /* 解析一帧, 1: 成功 */

#endif /* __RC_H__ */
