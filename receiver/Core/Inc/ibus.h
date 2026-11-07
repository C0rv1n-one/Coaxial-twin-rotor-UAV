/**
  ******************************************************************************
  * @file    ibus.h
  * @brief   iBUS Ò£¿ØÖ¡·¢ËÍ£¨USART2 -> F3 ·É¿Ø UART2£©
  ******************************************************************************
  */

#ifndef __IBUS_H__
#define __IBUS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void     IBUS_Init(void);
void     IBUS_Task(void);
uint32_t IBUS_GetFrames(void);

#ifdef __cplusplus
}
#endif

#endif /* __IBUS_H__ */
