/**
  ******************************************************************************
  * @file    msp.h
  * @brief   MSP v1（MultiWii Serial Protocol）链路：STM32 遥控器 <-> Betaflight 飞控
  *
  *  接线（两边都是 3.3V 电平，必须共地，千万不要接 5V）：
  *      STM32 PB10 (USART3_TX)  ---->  F3  UART3 RX
  *      STM32 PB11 (USART3_RX)  <----  F3  UART3 TX
  *      GND                     ----   GND
  *
  *  串口：USART3，115200 8N1，一帧一根线，双向收发。
  *
  *  下行 50Hz  MSP_SET_RAW_RC(200)
  *      负载 = 8 个通道 x int16 小端，取值 1000~2000
  *      通道顺序按飞控的 AETR：ch1=roll  ch2=pitch  ch3=throttle  ch4=yaw
  *      后面 4 个是 AUX1~AUX4（注意 Rc.aux[4] 是电池电压，不能当通道发）
  *
  *  上行 10Hz  请求 MSP_ATTITUDE(108)
  *      回包 6 字节：roll x 0.1 度、pitch x 0.1 度、yaw x 1 度，均为 int16 小端
  *
  *  帧格式（MSP v1）：
  *      '$' 'M' '<'  size  cmd  payload[0..size-1]  xor
  *      xor = size ^ cmd ^ payload[0] ^ ... ^ payload[size-1]
  *      其中 '<' 是主机->飞控，'>' 是飞控->主机
  ******************************************************************************
  */

#ifndef __MSP_H__
#define __MSP_H__

#include <stdint.h>

/* ---- MSP 命令号 ---------------------------------------------------------- */
#define MSP_CMD_STATUS            101U
#define MSP_CMD_ATTITUDE          108U
#define MSP_CMD_ANALOG            110U
#define MSP_CMD_SET_RAW_RC        200U

/* 一帧最多多少负载字节（MSP_SET_RAW_RC 8 通道 = 16 字节） */
#define MSP_MAX_PAYLOAD            64U

/* 发给飞控的通道数：AETR 4 个 + AUX1~AUX4 */
#define MSP_RC_CHANNELS             8U

/* 初始化：占用 USART3，启动单字节中断接收。必须在 MX_USART3_UART_Init() 之后调用 */
void     MSP_Init(void);

/* 主循环里反复调用，非阻塞：收帧解析 + 到点发帧 */
void     MSP_Task(void);

/* 1 = 最近 500ms 内收到过飞控的姿态回包（说明 F3 那边通着） */
uint8_t  MSP_AttValid(void);

/* 取姿态：roll/pitch 单位 0.1 度，yaw 单位 1 度 */
void     MSP_GetAttitude(int16_t *roll_01deg, int16_t *pitch_01deg, int16_t *yaw_deg);

/* 统计量，方便在 OLED 上看链路质量 */
uint32_t MSP_GetTxFrames(void);      /* 已发出的摇杆帧数 */
uint32_t MSP_GetAttFrames(void);     /* 收到的姿态帧数 */
uint32_t MSP_GetErrFrames(void);     /* 校验错 / 长度错的帧数 */

#endif /* __MSP_H__ */
