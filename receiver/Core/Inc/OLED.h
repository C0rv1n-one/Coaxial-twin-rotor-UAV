#ifndef __OLED_H
#define __OLED_H

#include "main.h"

/*----------------------------------------------------------------------------
 *  OLED 显示驱动（HAL 版）
 *  移植自“4-1 OLED显示屏”标准库例程，底层由软件 I2C 改为硬件 I2C1
 *
 *  坐标约定：
 *    Line   行号，范围 1~4
 *    Column 列号，范围 1~16（每个字符 8x16 像素）
 *
 *  使用前请先调用 OLED_Init()，之后可直接调用以下函数：
 *    OLED_ShowChar / OLED_ShowString / OLED_ShowNum
 *    OLED_ShowSignedNum / OLED_ShowHexNum / OLED_ShowBinNum / OLED_Clear
 *----------------------------------------------------------------------------*/
void OLED_Init(void);
void OLED_Clear(void);
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char);
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String);
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length);
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length);

#endif
