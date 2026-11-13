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
#include <stdint.h>
#include <stdio.h>
#include "VL53L1X_api.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// ADRESELE I2C ALE SENZORILOR VL53L1X 
#define ADDR_SHIELD 0x56   // senzor centru 
#define ADDR_L      0x54   // senzor stanga
#define ADDR_R      0x58   // senzor dreapta 
// ADRESELE EXPANDERELOR/ SHIELD
#define EXP_LR      0x86   // expander U21: stanga + dreapta
#define EXP_C       0x84   // expander U19: centru
#define EXP_GPSR    0x12
#define EXP_GPDR    0x14
// PINII XSHUT DE PE EXPANDERE
#define XSHUT_L     (1u << 14)   // pe EXP_LR
#define XSHUT_R     (1u << 15)   // pe EXP_LR
#define XSHUT_C     (1u << 15)   // pe EXP_C

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// FUNCTIE AJUTATOARE PENTRU SETAREA/RESETAREA BITILOR DIN REGISTRUL "reg" DE PE EXPANDERUL "exp"
static void exp_bits(uint8_t exp, uint8_t reg, uint16_t mask, uint8_t set)
{
    uint8_t b[2]; // BUFFER PENTRU CITIREA SI SCRIEREA PE I2C
    HAL_I2C_Mem_Read(&hi2c1, exp, reg, I2C_MEMADD_SIZE_8BIT, b, 2, 100);
    // ^ CITIREA A 2 BYTES DIN REGISTRUL "reg" DE PE EXPANDERUL "exp"
    uint16_t v = b[0] | (b[1] << 8);
    // ^ COMBINAREA CELOR 2 BYTES INTR-UN SINGUR VALOARE DE 16 BITS
    if (set) v |= mask; else v &= ~mask; // SETAREA SAU RESETAREA BITILOR DIN "mask" IN FUNCTIE DE "set"
    b[0] = v & 0xFF;
    b[1] = v >> 8;
    // ^ IMPARTIREA VALORII DE 16 BITS INAPOI IN 2 BYTES PENTRU SCRIEREA IN REGISTRUL "reg"
    HAL_I2C_Mem_Write(&hi2c1, exp, reg, I2C_MEMADD_SIZE_8BIT, b, 2, 100);
    // ^ SCRIERE EFECTIVA A CELOR 2 BYTES PE REGISTRUL "reg" DE PE EXPANDERUL "exp"
}

// ENUMERARE A TIPURILOR DE GEST
typedef enum { G_NONE, G_NEXT, G_PREV} gesture_t;

// CONSTANTE PENTRU DETECTAREA GESTURILOR
#define GEST_MS 800
#define DIST_TH 700
#define LOST_MS 500 
#define COOLDOWN_MS 700 // PAUZA DUPA GEST
#define SWIPE_MIN_MS 60   // sub această durată e apropiere, nu swipe

// variabila globala pentru a bloca detectarea gesturilor in timpul unui zoom
static uint32_t block_until = 0;

// FUNCTIE AJUTATOARE PENTRU DETECTAREA GESTURILOR
static gesture_t detect_gesture(const uint16_t distances[3], uint32_t now){
  static uint32_t t[3] = {0, 0, 0}; // TIMPURILE PENTRU FIECARE SENZOR (STANGA, CENTRU, DREAPTA)
  static uint32_t last_seen = 0; // ULTIMA DATA CAND A FOST VAZUT UN OBIECT
  static uint8_t wait_release = 0; 

  if (now < block_until) return G_NONE; // DACA SUNTEM IN COOLDOWN, NU DETECTAM GESTURI

  uint8_t any = 0;
  for (int k = 0; k < 3; k++){
    if (distances[k] > 0 && distances[k] < DIST_TH){ // VERIFICARE DISTANTA < THRESHOLD
      any = 1;
      last_seen = now;
      if (!wait_release && t[k] == 0) t[k] = now; // DACA NU A FOST ELIBERAT, NU RESETEAZA TIMPUL
    }
  }

  if (wait_release){
    if (!any && now - last_seen > 150) wait_release = 0;
    return G_NONE;
  }

  gesture_t gesture = G_NONE;
  if (t[0] && t[1] && t[2]){
    if (t[0] < t[1] && t[1] < t[2]){
      uint32_t span = t[2] - t[0];
      if (span >= SWIPE_MIN_MS && span < GEST_MS) gesture = G_NEXT;
    }
    else if (t[2] < t[1] && t[1] < t[0]){
      uint32_t span = t[0] - t[2];
      if (span >= SWIPE_MIN_MS && span < GEST_MS) gesture = G_PREV;
    }
    t[0] = t[1] = t[2] = 0;
    if (gesture != G_NONE) block_until = now + COOLDOWN_MS;
    else wait_release = 1;
  }
  else if (last_seen && now - last_seen > LOST_MS){
    t[0] = t[1] = t[2] = 0;
    last_seen = 0;
  }
  return gesture;
}

#define ZOOM_DELTA 80 // 

typedef enum { ZOOM_NONE, ZOOM_IN, ZOOM_OUT } zoom_t;

// FUNCTIE ZOOM IN/ ZOOM OUT PENTRU AFISAREA IN UART
static zoom_t detect_zoom(const uint16_t distances[3], uint32_t now){
  static uint16_t last_distances[3] = {0, 0, 0};
  static uint16_t start_c = 0;
  static uint8_t active = 0;
  static uint32_t zoom_block = 0;

  for (int k=0; k<3; k++){
    if (distances[k]) last_distances[k] = distances[k];
  }
  if (now < zoom_block || now < block_until){
    active = 0;                 // ca să reînceapă curat după pauză
    return ZOOM_NONE;
  }

  uint8_t all = 1;
  for (int k=0; k<3; k++){
    if (last_distances[k] == 0 || last_distances[k] >= DIST_TH) all = 0;
  }

  if(!all){
    active = 0;
    start_c = 0;
    return ZOOM_NONE;
  }
  if(!active){
    active = 1;
    start_c = last_distances[1];
    return ZOOM_NONE;
  }

  int diff = (int)last_distances[1] - (int)start_c;
  if (diff <= -ZOOM_DELTA) { active = 0; zoom_block = now + COOLDOWN_MS; return ZOOM_IN; }
  if (diff >=  ZOOM_DELTA) { active = 0; zoom_block = now + COOLDOWN_MS; return ZOOM_OUT; }
  return ZOOM_NONE;
}

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
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  
  // VARIABILE PENTRU AFISAREA IN UART
  char msg[64]; // BUFFER PENTRU MESAJELE DE DEBUG
  int n; // NUMARUL DE CARACTERE DIN MESAJELE DE DEBUG
  uint8_t booted; // VARIABILA PENTRU VERIFICAREA STARI DE BOOT A SENZORILOR

  // 1. TREZESTE TOTI SENZORII (XSHUT=1) SI ASTEPTA 10 ms
  exp_bits(EXP_LR, EXP_GPDR, XSHUT_L | XSHUT_R, 1);
  exp_bits(EXP_C,  EXP_GPDR, XSHUT_C, 1);
  exp_bits(EXP_LR, EXP_GPSR, XSHUT_L | XSHUT_R, 0);
  exp_bits(EXP_C,  EXP_GPSR, XSHUT_C, 0);
  HAL_Delay(10);

  // 2. CENTRU: TREZESTE, SCHIMBA ADRESA I2C
  exp_bits(EXP_C, EXP_GPSR, XSHUT_C, 1);
  HAL_Delay(10);
  VL53L1X_SetI2CAddress(0x52, ADDR_SHIELD);

  // 3. STANGA: TREZESTE, SCHIMBA ADRESA I2C
  exp_bits(EXP_LR, EXP_GPSR, XSHUT_L, 1);
  HAL_Delay(10);
  VL53L1X_SetI2CAddress(0x52, ADDR_L);

  // 4. DREAPTA: TREZESTE, SCHIMBA ADRESA I2C
  exp_bits(EXP_LR, EXP_GPSR, XSHUT_R, 1);
  HAL_Delay(10);
  VL53L1X_SetI2CAddress(0x52, ADDR_R);

  // 5. initializeaza toti trei
  uint8_t addrs[3] = {ADDR_SHIELD, ADDR_L, ADDR_R};
  for (int k = 0; k < 3; k++) {
      booted = 0;
      for (int i = 0; i < 100 && !booted; i++) {
          VL53L1X_BootState(addrs[k], &booted);
          HAL_Delay(2);
      }
      if (booted) {
          VL53L1X_SensorInit(addrs[k]); // INITIALIZARE SENZOR
          VL53L1X_SetDistanceMode(addrs[k], 1); // MOD DE DISTANTA 1 ~1-2 m
          VL53L1X_SetTimingBudgetInMs(addrs[k], 20); // 20 ms PETRECUTE PER MASURATOARE
          VL53L1X_SetInterMeasurementInMs(addrs[k], 25); // MASURATOARE ODATA LA 25 ms
          VL53L1X_StartRanging(addrs[k]); // PORNIRE MASURATORI
      }
      n = snprintf(msg, sizeof(msg), "Senzor 0x%02X: %s\r\n", addrs[k], booted ? "OK" : "nu a pornit");
      HAL_UART_Transmit(&huart2, (uint8_t*)msg, n, 100);
  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */
    /* USER CODE BEGIN 3 */
    uint16_t distance[3] = {0, 0, 0}; // MATRICEA DE DISTANTA
    uint8_t addrs3[3] = {ADDR_L, ADDR_SHIELD, ADDR_R}; // ADRESE IN ORDINE STANGA, CENTRU, DREAPTA
    uint8_t ready; // VARIABILA PENTRU VERIFICAREA STARI
    char out[64];

    for (int k = 0; k < 3; k++) {
        ready = 0;
        if (VL53L1X_CheckForDataReady(addrs3[k], &ready) == 0 && ready) { // VERIFICA DACA DATELE SUNT GATA
            VL53L1X_GetDistance(addrs3[k], &distance[k]); // IA DISTANTA
            VL53L1X_ClearInterrupt(addrs3[k]); // CURATA INTERRUPTUL PENTRU A PERMITE URMATOAREA MASURARE
        }
    }
    gesture_t gesture = detect_gesture(distance, HAL_GetTick());
    if (gesture == G_NEXT) HAL_UART_Transmit(&huart2, (uint8_t*)"NEXT\r\n", 6, 100);
    if (gesture == G_PREV) HAL_UART_Transmit(&huart2, (uint8_t*)"PREV\r\n", 6, 100);
    if (gesture == G_NONE) {
        zoom_t z = detect_zoom(distance, HAL_GetTick());
        if (z == ZOOM_IN)  HAL_UART_Transmit(&huart2, (uint8_t*)"ZOOM_IN\r\n", 9, 100);
        if (z == ZOOM_OUT) HAL_UART_Transmit(&huart2, (uint8_t*)"ZOOM_OUT\r\n", 10, 100);
    }
    HAL_Delay(5);

    // DEBUG: AFISEAZA DISTANTELE IN UART DACA ORICARE DINTRE ELE ESTE MAI MIC DE DIST_TH
    // if ((distance[0] && distance[0] < DIST_TH) ||
    //     (distance[1] && distance[1] < DIST_TH) ||
    //     (distance[2] && distance[2] < DIST_TH)) {
    //     int m = snprintf(out, sizeof(out), "L:%u C:%u R:%u\r\n", distance[0], distance[1], distance[2]);
    //     HAL_UART_Transmit(&huart2, (uint8_t*)out, m, 100);
    // }
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL12;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_I2C1;
  PeriphClkInit.I2c1ClockSelection = RCC_I2C1CLKSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
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
  hi2c1.Init.Timing = 0x0010020A;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 38400;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : B1_Pin */
  GPIO_InitStruct.Pin = B1_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
