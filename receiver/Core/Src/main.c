/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "OLED.h"
#include "nrf24l01.h"
#include "rc.h"
#include "msp.h"
#include "ibus.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static void Show_RC(void);
static void Show_Line4(void);
static uint8_t  nrf_ok    = 0;
static uint8_t  nrf_cfg   = 0;
static uint8_t  oled_ok   = 0;
static uint32_t show_tick = 0;
static uint32_t led_tick  = 0;
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_SPI1_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  OLED_Init();                     /* OLED: hardware I2C1, slave 0x78 */

  /* NRF24L01: slow SPI down to 72/32 = 2.25 MHz for long dupont wires */
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
  HAL_SPI_Init(&hspi1);
  oled_ok = (HAL_I2C_IsDeviceReady(&hi2c1, 0x78, 3U, 50U) == HAL_OK) ? 1U : 0U;

  /* Let the nRF24L01 supply settle before the presence check, then retry
     once: a module that is still booting would otherwise latch nrf_ok = 0. */
  HAL_Delay(20);
  nrf_ok = NRF24L01_Check();       /* 1 = NRF24L01 module present */
  if (!nrf_ok)
  {
    HAL_Delay(30);
    nrf_ok = NRF24L01_Check();
  }
  NRF24L01_Init(0);                /* start on channel 0 (ANO default) */
  RC_Init();
  MSP_Init();                      /* USART3 <-> F3 flight controller, MSP (attitude) */
  IBUS_Init();                     /* USART2 (PA2) -> F3 UART2, iBUS stick frames */
  Show_RC();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    RC_Task();                     /* receive + parse + auto channel scan */
    MSP_Task();                    /* attitude request/reply on USART3 */
    IBUS_Task();                   /* 100Hz stick frames to FC on USART2 */

    if ((HAL_GetTick() - show_tick) >= 100U)
    {
      show_tick = HAL_GetTick();
      Show_RC();
    }

    /* PC13 heartbeat: 1 Hz = OK, 4 Hz = OLED not answering on I2C */
    if ((HAL_GetTick() - led_tick) >= (oled_ok ? 500U : 125U))
    {
      led_tick = HAL_GetTick();
      HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 400000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief USART3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 115200;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CSN_GPIO_Port, CSN_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(CE_GPIO_Port, CE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : CSN_Pin */
  GPIO_InitStruct.Pin = CSN_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CSN_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : CE_Pin */
  GPIO_InitStruct.Pin = CE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(CE_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : NRF_IRQ_Pin */
  GPIO_InitStruct.Pin = NRF_IRQ_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(NRF_IRQ_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI1_IRQn, 2, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Refresh RC data on OLED (4 lines x 16 chars)
  *         line1: link status + channel
  *         line2: THR / YAW   line3: ROL / PIT
  *         line4: F3 attitude (2 s) / AUX1 + battery / RF counters, see Show_Line4()
  */
static void Show_RC(void)
{
  /* Live SPI probe first: 0x00 / 0xFF means the module is not answering at
     all. Any other value (0x0E / 0x0F ...) proves the module answers *now*,
     so a failed one-shot boot check must not keep showing "ER" for ever. */
  nrf_cfg = NRF24L01_ReadRegister(NRF_CONFIG);

  if ((nrf_cfg == 0x00U) || (nrf_cfg == 0xFFU))
  {
    nrf_ok = 0U;
    OLED_ShowString(1, 1, "ER ");
  }
  else
  {
    nrf_ok = 1U;
    if (RC_IsOnline()) OLED_ShowString(1, 1, "RC ");
    else               OLED_ShowString(1, 1, "NO ");
  }

  OLED_ShowString(1, 4, "C:");
  OLED_ShowHexNum(1, 6, nrf_cfg, 2);
  OLED_ShowString(1, 9, "CH:");
  OLED_ShowNum(1, 12, (uint32_t)NRF24L01_GetChannel(), 3);
  OLED_ShowString(1, 15, "M");
  OLED_ShowNum(1, 16, (uint32_t)RC_GetPayloadMode(), 1);

  OLED_ShowString(2, 1, "T:");
  OLED_ShowNum(2, 3, (uint32_t)Rc.thr, 4);
  OLED_ShowString(2, 8, "Y:");
  OLED_ShowNum(2, 10, (uint32_t)Rc.yaw, 4);

  OLED_ShowString(3, 1, "R:");
  OLED_ShowNum(3, 3, (uint32_t)Rc.rol, 4);
  OLED_ShowString(3, 8, "P:");
  OLED_ShowNum(3, 10, (uint32_t)Rc.pit, 4);

  Show_Line4();
}

/**
  * @brief  Line 4, alternates every 2 s:
  *           phase 0 and F3 answering -> attitude  R+12 P-03 Y123
  *           otherwise                -> AUX1 / battery, or RF counters
  *         Whole 16 chars are always rewritten so no stale text is left over.
  */
static void Show_Line4(void)
{
  char    s[17];
  int16_t r, p, y;
  int     i;
  uint8_t phase = (uint8_t)((HAL_GetTick() / 2000U) & 1U);

  if (MSP_AttValid() && (phase == 0U))
  {
    MSP_GetAttitude(&r, &p, &y);
    r = (int16_t)(r / 10);         /* 0.1 deg -> deg */
    p = (int16_t)(p / 10);
    if (r >  99) { r =  99; }
    if (r < -99) { r = -99; }
    if (p >  99) { p =  99; }
    if (p < -99) { p = -99; }
    (void)sprintf(s, "R%+3d P%+3d Y%3d", (int)r, (int)p, (int)y);
  }
  else if (RC_IsOnline())
  {
    (void)sprintf(s, "A1:%4d B:%2d", (int)Rc.aux[0], (int)(Rc.aux[4] / 100));
  }
  else
  {
    (void)sprintf(s, "RF:%4lu S:%02X", (unsigned long)NRF24L01_GetRxEvents(),
                  (unsigned)NRF24L01_GetLastStatus());
  }

  for (i = (int)strlen(s); i < 16; i++) { s[i] = ' '; }
  s[16] = '\0';
  OLED_ShowString(4, 1, s);
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Fast blink on PC13 at register level: works even if the clock setup failed.
     Means: HSE/PLL or a peripheral init failed -> CPU never reaches main loop. */
  RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
  GPIOC->CRH &= ~(0xFU << 20);
  GPIOC->CRH |=  (0x6U << 20);
  for (;;)
  {
    GPIOC->BSRR = GPIO_BSRR_BR13;
    for (volatile uint32_t i = 0; i < 100000U; i++) { __NOP(); }
    GPIOC->BSRR = GPIO_BSRR_BS13;
    for (volatile uint32_t i = 0; i < 100000U; i++) { __NOP(); }
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
