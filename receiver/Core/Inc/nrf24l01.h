/**
  ******************************************************************************
  * @file    nrf24l01.h
  * @brief   NRF24L01 (SI24R1) 接收驱动，HAL 版
  *
  *  射频参数完全照搬 C:\Users\y9000p\Desktop\remote and STM32\Remoter_Ctrl3.6
  *  \Driver\ANO_Drv_Nrf24l01.c，保证能和 ANO Remoter 遥控器通信：
  *    地址   AA BB CC 00 01
  *    速率   2 Mbps / 0 dBm        (RF_SETUP = 0x0F)
  *    信道   0 ~ 125 (遥控器默认 0)
  *    CRC    16 位                 (CONFIG = 0x0F)
  *    载荷   动态长度              (FEATURE = 0x04, DYNPD = 0x01)
  ******************************************************************************
  */

#ifndef __NRF24L01_H__
#define __NRF24L01_H__

#include <stdint.h>
#include "main.h"

/* -------------------------- SPI 命令 -------------------------- */
#define NRF_R_REGISTER          0x00
#define NRF_W_REGISTER          0x20
#define NRF_R_RX_PL_WID         0x60
#define NRF_R_RX_PAYLOAD        0x61
#define NRF_W_TX_PAYLOAD        0xA0
#define NRF_W_ACK_PAYLOAD       0xA8
#define NRF_W_TX_PAYLOAD_NOACK  0xB0
#define NRF_FLUSH_TX            0xE1
#define NRF_FLUSH_RX            0xE2
#define NRF_REUSE_TX_PL         0xE3
#define NRF_NOP                 0xFF

/* -------------------------- 寄存器地址 ------------------------ */
#define NRF_CONFIG              0x00
#define NRF_EN_AA               0x01
#define NRF_EN_RXADDR           0x02
#define NRF_SETUP_AW            0x03
#define NRF_SETUP_RETR          0x04
#define NRF_RF_CH               0x05
#define NRF_RF_SETUP            0x06
#define NRF_STATUS              0x07
#define NRF_OBSERVE_TX          0x08
#define NRF_RPD                 0x09
#define NRF_RX_ADDR_P0          0x0A
#define NRF_RX_ADDR_P1          0x0B
#define NRF_TX_ADDR             0x10
#define NRF_RX_PW_P0            0x11
#define NRF_FIFO_STATUS         0x17
#define NRF_DYNPD               0x1C
#define NRF_FEATURE             0x1D

/* -------------------------- CONFIG 位 ------------------------- */
#define NRF_CONFIG_MASK_RX_DR   0x40
#define NRF_CONFIG_MASK_TX_DS   0x20
#define NRF_CONFIG_MASK_MAX_RT  0x10
#define NRF_CONFIG_EN_CRC       0x08
#define NRF_CONFIG_CRCO         0x04
#define NRF_CONFIG_PWR_UP       0x02
#define NRF_CONFIG_PRIM_RX      0x01

/* -------------------------- STATUS 位 ------------------------- */
#define NRF_STATUS_RX_DR        0x40
#define NRF_STATUS_TX_DS        0x20
#define NRF_STATUS_MAX_RT       0x10

/* -------------------------- FEATURE 位 ------------------------ */
#define NRF_FEATURE_EN_DYN_ACK  0x01
#define NRF_FEATURE_EN_ACK_PAY  0x02
#define NRF_FEATURE_EN_DPL      0x04

/* 最大载荷宽度 */
#define NRF_PAYLOAD_SIZE        32

void    NRF24L01_Init(uint8_t channel);           /* 初始化成接收端 */
uint8_t NRF24L01_Check(void);                     /* 1: 模块正常, 0: 通信失败 */
void    NRF24L01_SetChannel(uint8_t channel);     /* 切换信道 (0~125) */
uint8_t NRF24L01_GetChannel(void);                /* 当前信道 */
uint8_t NRF24L01_ReadPacket(uint8_t *buf, uint8_t *len); /* 1: 收到一包 */
uint8_t NRF24L01_ReadRegister(uint8_t reg);
void     NRF24L01_EnterTx(void);
void     NRF24L01_EnterRx(void);
uint8_t NRF24L01_IsTxMode(void);
uint8_t NRF24L01_SendPacket(const uint8_t *buf, uint8_t len);
void     NRF24L01_SetPayloadMode(uint8_t fixed_mode); /* 0=?? 1=??25?? */
uint16_t NRF24L01_GetRxEvents(void);
uint16_t NRF24L01_GetRxBad(void);
uint8_t  NRF24L01_GetLastStatus(void);              /* RF ????? */
uint8_t NRF24L01_GetRPD(void);                 /* 1: ???? >-64dBm ?? */       /* debug: raw register read */
void    NRF24L01_LoadAckPayload(const uint8_t *data, uint8_t len); /* 回传给遥控器的数据 */

#endif /* __NRF24L01_H__ */
