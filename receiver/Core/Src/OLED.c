/*----------------------------------------------------------------------------
 *  OLED.c  OLED 显示驱动（HAL 库版本）
 *
 *  把“4-1 OLED显示屏”标准库例程里的字符显示函数模块（OLED_ShowChar、
 *  OLED_ShowString、OLED_ShowNum、OLED_ShowSignedNum、OLED_ShowHexNum、
 *  OLED_ShowBinNum 等）移植到本 CubeMX 工程：
 *
 *  1. 底层通信：去掉原例程的 GPIO 软件 I2C（PB8/PB9 位翻转），改为调用
 *     HAL_I2C_Master_Transmit() 驱动硬件 I2C1（CubeMX 已配置 PB8=SCL、
 *     PB9=SDA，快速模式 400kHz），无需再开启 GPIOB 时钟、无需软件时序。
 *  2. 延时函数：Delay_ms() 换成 HAL_Delay()。
 *  3. 上层字符显示函数（坐标、字模、取模方式）与原例程完全一致，可直接复用
 *     原有调用代码，例如：
 *        OLED_ShowString(1, 1, "Hello");
 *        OLED_ShowNum(2, 1, 1234, 4);
 *----------------------------------------------------------------------------*/
#include "main.h"
#include "OLED.h"
#include "OLED_Font.h"

/* OLED 从机地址：0x3C 左移一位（HAL 传参包含读写位，写操作为 0x78） */
#define OLED_I2C_ADDR      0x78U
/* 单次 I2C 传输超时时间（ms），避免 OLED 掉线时程序死等 */
#define OLED_I2C_TIMEOUT   100U

/* CubeMX 在 main.c 中生成的 I2C1 句柄 */
extern I2C_HandleTypeDef hi2c1;

/**
  * @brief  OLED 写命令（控制字节 0x00 + 命令字节）
  * @param  Command 要写入的命令
  * @retval 无
  */
static void OLED_WriteCommand(uint8_t Command)
{
    uint8_t buf[2] = {0x00, Command};

    (void)HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buf, 2U, OLED_I2C_TIMEOUT);
}

/**
  * @brief  一次 I2C 事务连续写入多个数据字节
  *         （控制字节 0x40 + Length 个数据），比逐字节发送快很多
  * @param  Data   数据缓冲区
  * @param  Length 数据长度，最大 128（一页像素）
  * @retval 无
  */
static void OLED_WriteDataBuf(const uint8_t *Data, uint8_t Length)
{
    uint8_t buf[1 + 128];
    uint8_t i;

    if (Length > 128U)
    {
        Length = 128U;
    }

    buf[0] = 0x40;
    for (i = 0; i < Length; i++)
    {
        buf[i + 1U] = Data[i];
    }

    (void)HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR, buf,
                                  (uint16_t)(Length + 1U), OLED_I2C_TIMEOUT);
}

/**
  * @brief  OLED 设置光标位置
  * @param  Y 以左上角为原点，向下方向的坐标，范围：0~7（页）
  * @param  X 以左上角为原点，向右方向的坐标，范围：0~127
  * @retval 无
  */
void OLED_SetCursor(uint8_t Y, uint8_t X)
{
    OLED_WriteCommand((uint8_t)(0xB0 | Y));                     /* 设置 Y 位置 */
    OLED_WriteCommand((uint8_t)(0x10 | ((X & 0xF0U) >> 4)));    /* 设置 X 位置高 4 位 */
    OLED_WriteCommand((uint8_t)(0x00 | (X & 0x0FU)));           /* 设置 X 位置低 4 位 */
}

/**
  * @brief  OLED 清屏
  * @param  无
  * @retval 无
  */
void OLED_Clear(void)
{
    static const uint8_t zeros[128] = {0};
    uint8_t j;

    for (j = 0; j < 8U; j++)
    {
        OLED_SetCursor(j, 0U);
        OLED_WriteDataBuf(zeros, 128U);   /* 一整页 128 字节一次写完 */
    }
}

/**
  * @brief  OLED 显示一个字符
  * @param  Line   行位置，范围：1~4
  * @param  Column 列位置，范围：1~16
  * @param  Char   要显示的一个字符，范围：ASCII 可见字符
  * @retval 无
  */
void OLED_ShowChar(uint8_t Line, uint8_t Column, char Char)
{
    uint8_t index;

    if ((Char < ' ') || (Char > '~'))    /* 超出字库范围时用 '?' 代替 */
    {
        Char = '?';
    }
    index = (uint8_t)(Char - ' ');

    OLED_SetCursor((uint8_t)((Line - 1U) * 2U), (uint8_t)((Column - 1U) * 8U));   /* 上半部分 */
    OLED_WriteDataBuf(&OLED_F8x16[index][0], 8U);

    OLED_SetCursor((uint8_t)((Line - 1U) * 2U + 1U), (uint8_t)((Column - 1U) * 8U)); /* 下半部分 */
    OLED_WriteDataBuf(&OLED_F8x16[index][8], 8U);
}

/**
  * @brief  OLED 显示字符串
  * @param  Line   起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  String 要显示的字符串，范围：ASCII 可见字符
  * @retval 无
  */
void OLED_ShowString(uint8_t Line, uint8_t Column, char *String)
{
    uint8_t i;

    for (i = 0; String[i] != '\0'; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i), String[i]);
    }
}

/**
  * @brief  OLED 次方函数
  * @retval 返回值等于 X 的 Y 次方
  */
uint32_t OLED_Pow(uint32_t X, uint32_t Y)
{
    uint32_t Result = 1;

    while (Y--)
    {
        Result *= X;
    }

    return Result;
}

/**
  * @brief  OLED 显示数字（十进制，正数）
  * @param  Line   起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~4294967295
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i),
                      (char)(Number / OLED_Pow(10, (uint32_t)(Length - i - 1)) % 10 + '0'));
    }
}

/**
  * @brief  OLED 显示数字（十进制，带符号数）
  * @param  Line   起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：-2147483648~2147483647
  * @param  Length 要显示数字的长度，范围：1~10
  * @retval 无
  */
void OLED_ShowSignedNum(uint8_t Line, uint8_t Column, int32_t Number, uint8_t Length)
{
    uint8_t i;
    uint32_t Number1;

    if (Number >= 0)
    {
        OLED_ShowChar(Line, Column, '+');
        Number1 = (uint32_t)Number;
    }
    else
    {
        OLED_ShowChar(Line, Column, '-');
        Number1 = (uint32_t)(-Number);
    }

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i + 1U),
                      (char)(Number1 / OLED_Pow(10, (uint32_t)(Length - i - 1)) % 10 + '0'));
    }
}

/**
  * @brief  OLED 显示数字（十六进制，正数）
  * @param  Line   起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~0xFFFFFFFF
  * @param  Length 要显示数字的长度，范围：1~8
  * @retval 无
  */
void OLED_ShowHexNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;
    uint8_t SingleNumber;

    for (i = 0; i < Length; i++)
    {
        SingleNumber = (uint8_t)(Number / OLED_Pow(16, (uint32_t)(Length - i - 1)) % 16);
        if (SingleNumber < 10U)
        {
            OLED_ShowChar(Line, (uint8_t)(Column + i), (char)(SingleNumber + '0'));
        }
        else
        {
            OLED_ShowChar(Line, (uint8_t)(Column + i), (char)(SingleNumber - 10 + 'A'));
        }
    }
}

/**
  * @brief  OLED 显示数字（二进制，正数）
  * @param  Line   起始行位置，范围：1~4
  * @param  Column 起始列位置，范围：1~16
  * @param  Number 要显示的数字，范围：0~1111 1111 1111 1111
  * @param  Length 要显示数字的长度，范围：1~16
  * @retval 无
  */
void OLED_ShowBinNum(uint8_t Line, uint8_t Column, uint32_t Number, uint8_t Length)
{
    uint8_t i;

    for (i = 0; i < Length; i++)
    {
        OLED_ShowChar(Line, (uint8_t)(Column + i),
                      (char)(Number / OLED_Pow(2, (uint32_t)(Length - i - 1)) % 2 + '0'));
    }
}

/**
  * @brief  OLED 初始化
  *         注意：调用前必须已完成 MX_I2C1_Init()（HAL_I2C_Init）
  * @param  无
  * @retval 无
  */
void OLED_Init(void)
{
    HAL_Delay(100);   /* 上电延时，等待 OLED 复位完成 */

    OLED_WriteCommand(0xAE);    /* 关闭显示 */

    OLED_WriteCommand(0xD5);    /* 设置显示时钟分频比/振荡器频率 */
    OLED_WriteCommand(0x80);

    OLED_WriteCommand(0xA8);    /* 设置多路复用率 */
    OLED_WriteCommand(0x3F);

    OLED_WriteCommand(0xD3);    /* 设置显示偏移 */
    OLED_WriteCommand(0x00);

    OLED_WriteCommand(0x40);    /* 设置显示开始行 */

    OLED_WriteCommand(0xA1);    /* 设置左右方向，0xA1 正常 0xA0 左右反置 */

    OLED_WriteCommand(0xC8);    /* 设置上下方向，0xC8 正常 0xC0 上下反置 */

    OLED_WriteCommand(0xDA);    /* 设置 COM 引脚硬件配置 */
    OLED_WriteCommand(0x12);

    OLED_WriteCommand(0x81);    /* 设置对比度控制 */
    OLED_WriteCommand(0xCF);

    OLED_WriteCommand(0xD9);    /* 设置预充电周期 */
    OLED_WriteCommand(0xF1);

    OLED_WriteCommand(0xDB);    /* 设置 VCOMH 取消选择级别 */
    OLED_WriteCommand(0x30);

    OLED_WriteCommand(0xA4);    /* 设置整个显示打开/关闭 */

    OLED_WriteCommand(0xA6);    /* 设置正常/倒转显示 */

    OLED_WriteCommand(0x8D);    /* 设置充电泵 */
    OLED_WriteCommand(0x14);

    OLED_WriteCommand(0xAF);    /* 开启显示 */

    OLED_Clear();               /* OLED 清屏 */
}
