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
#include "ssd1306.h"
#include "dfplayer.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    SYS_IDLE,
    SYS_UNLOCKING,
    SYS_ENROLLING,
    SYS_DELETING,
    SYS_DENIED
} SystemState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RELAY_OPEN_MS       3000U   /* Door stays unlocked for 3 seconds       */
#define DELETE_HOLD_MS      3000U   /* Hold DELETE button 3s to confirm        */
#define DENIED_DISPLAY_MS   2500U   /* "Access Denied" shown for 2.5 seconds   */
#define ENROLL_TIMEOUT_MS  15000U   /* Enrolling mode auto-cancels after 15s   */
#define DEBOUNCE_MS          200U   /* Minimum ms between button events        */

#define UART1_BUF_SIZE        64U   /* ESP32 message receive buffer            */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

/* USER CODE BEGIN PV */
/* --- Button flags (set in ISR, cleared in main loop) --- */
static volatile uint8_t  btn_exit_flag   = 0;
static volatile uint8_t  btn_enroll_flag = 0;
static volatile uint8_t  btn_delete_flag = 0;
static volatile uint32_t btn_exit_tick   = 0;
static volatile uint32_t btn_enroll_tick = 0;
static volatile uint32_t btn_delete_tick = 0;

/* --- UART1 receive buffer (ESP32-S3 → STM32) --- */
static uint8_t  uart1_rx_byte = 0;
static char     uart1_line[UART1_BUF_SIZE];
static uint16_t uart1_line_idx = 0;
static volatile uint8_t uart1_line_ready = 0;

/* --- System state machine --- */
static SystemState_t sys_state  = SYS_IDLE;
static uint32_t      state_tick = 0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART3_UART_Init(void);
/* USER CODE BEGIN PFP */
static void App_Init(void);
static void App_Loop(void);
static void Relay_Open(void);
static void Relay_Close(void);
static void Enter_Unlocked(uint8_t face_id, uint8_t from_exit_btn);
static void Enter_Denied(void);
static void Parse_ESP32_Msg(const char *msg);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* -----------------------------------------------------------------------
 * HAL UART RX callback – called after each byte received on UART1 (ESP32)
 * Accumulates bytes into uart1_line until '\n', then sets uart1_line_ready.
 * ----------------------------------------------------------------------- */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        if (uart1_rx_byte == '\n') {
            uart1_line[uart1_line_idx] = '\0';
            uart1_line_idx  = 0;
            uart1_line_ready = 1;
        } else if (uart1_rx_byte != '\r') {
            if (uart1_line_idx < UART1_BUF_SIZE - 1) {
                uart1_line[uart1_line_idx++] = (char)uart1_rx_byte;
            } else {
                /* Buffer overflow guard: discard line */
                uart1_line_idx = 0;
            }
        }
        /* Restart reception for next byte */
        HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);
    }
}

/* -----------------------------------------------------------------------
 * HAL GPIO EXTI callback – called from EXTI IRQ handlers for all buttons.
 * Uses timestamps to debounce button presses.
 * ----------------------------------------------------------------------- */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    uint32_t now = HAL_GetTick();

    if (GPIO_Pin == BTN_EXIT_Pin) {
        if ((now - btn_exit_tick) >= DEBOUNCE_MS) {
            btn_exit_tick = now;
            btn_exit_flag = 1;
        }
    } else if (GPIO_Pin == BTN_ENROLL_Pin) {
        if ((now - btn_enroll_tick) >= DEBOUNCE_MS) {
            btn_enroll_tick = now;
            btn_enroll_flag = 1;
        }
    } else if (GPIO_Pin == BTN_DELETE_Pin) {
        if ((now - btn_delete_tick) >= DEBOUNCE_MS) {
            btn_delete_tick = now;
            btn_delete_flag = 1;
        }
    }
}

/* ----------------------------------------------------------------------- */

static void Relay_Open(void)
{
    /* Relay module is active-HIGH: set PB0 HIGH to energise coil → open lock */
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LD2_GPIO_Port,   LD2_Pin,   GPIO_PIN_SET);
}

static void Relay_Close(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD2_GPIO_Port,   LD2_Pin,   GPIO_PIN_RESET);
}

static void Enter_Unlocked(uint8_t face_id, uint8_t from_exit_btn)
{
    Relay_Open();
    sys_state  = SYS_UNLOCKING;
    state_tick = HAL_GetTick();

    if (from_exit_btn) {
        SSD1306_ShowUnlockedExit();
    } else {
        SSD1306_ShowUnlocked(face_id);
    }
    DFPlayer_Play(DFP_TRACK_OPEN_DOOR);
}

static void Enter_Denied(void)
{
    sys_state  = SYS_DENIED;
    state_tick = HAL_GetTick();
    SSD1306_ShowDenied();
    DFPlayer_Play(DFP_TRACK_DENIED);
}

/* -----------------------------------------------------------------------
 * Parse a complete message line received from ESP32-S3.
 *
 * Protocol (ESP32 → STM32):
 *   "OPEN:<id>"       Face recognised, <id> = face database ID
 *   "DENIED"          Face not recognised
 *   "ENROLLED:<id>"   Enrolment success, new face ID
 *   "DELETED"         All faces cleared from database
 *   "READY"           ESP32 finished booting
 *
 * Enrollment step guidance (ESP32 sends these during ENROLL flow):
 *   "ENROLL_FRONT"    Ask user to look straight (step 1 / 5)
 *   "ENROLL_LEFT"     Ask user to turn left     (step 2 / 5)
 *   "ENROLL_RIGHT"    Ask user to turn right    (step 3 / 5)
 *   "ENROLL_UP"       Ask user to tilt up       (step 4 / 5)
 *   "ENROLL_DOWN"     Ask user to tilt down     (step 5 / 5)
 * ----------------------------------------------------------------------- */
static void Parse_ESP32_Msg(const char *msg)
{
    if (strncmp(msg, "OPEN:", 5) == 0) {
        uint8_t id = (uint8_t)atoi(msg + 5);
        Enter_Unlocked(id, 0);

    } else if (strcmp(msg, "DENIED") == 0) {
        if (sys_state == SYS_IDLE) {
            Enter_Denied();
        }

    } else if (strncmp(msg, "ENROLLED:", 9) == 0) {
        uint8_t id = (uint8_t)atoi(msg + 9);
        sys_state  = SYS_IDLE;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrolled(id);
        DFPlayer_Play(DFP_TRACK_ENROLLED);

    } else if (strcmp(msg, "DELETED") == 0) {
        sys_state  = SYS_IDLE;
        state_tick = HAL_GetTick();
        SSD1306_ShowDeleted();
        DFPlayer_Play(DFP_TRACK_DELETED);

    } else if (strcmp(msg, "READY") == 0) {
        sys_state = SYS_IDLE;
        SSD1306_ShowReady();

    /* --- Enrollment step guidance (5 poses, sent sequentially by ESP32) --- */
    } else if (strcmp(msg, "ENROLL_FRONT") == 0) {
        state_tick = HAL_GetTick(); /* Reset enrol timeout each step */
        SSD1306_ShowEnrollStep(1, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_FRONT);

    } else if (strcmp(msg, "ENROLL_LEFT") == 0) {
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(2, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_LEFT);

    } else if (strcmp(msg, "ENROLL_RIGHT") == 0) {
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(3, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_RIGHT);

    } else if (strcmp(msg, "ENROLL_UP") == 0) {
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(4, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_UP);

    } else if (strcmp(msg, "ENROLL_DOWN") == 0) {
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(5, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_DOWN);
    }
}

/* -----------------------------------------------------------------------
 * Application initialisation (called once after all HAL inits)
 * ----------------------------------------------------------------------- */
static void App_Init(void)
{
    /* Buttons configured correctly in CubeMX (Falling edge + Pull-up).
     * Clear any spurious EXTI flags that may have been set at boot. */
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_ENROLL_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_DELETE_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_EXIT_Pin);
    btn_enroll_flag = 0;
    btn_delete_flag = 0;
    btn_exit_flag   = 0;

    /* --- Enable USART1 NVIC (ESP32 receive) --- */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* --- OLED initialisation and boot screen --- */
    SSD1306_Init();
    SSD1306_ShowBoot();

    /* --- DFPlayer initialisation (blocks ~1.6 s for SD card enumeration) --- */
    DFPlayer_Init();

    /* --- Start UART1 interrupt-driven receive --- */
    HAL_UART_Receive_IT(&huart1, &uart1_rx_byte, 1);

    /* --- Show ready screen --- */
    SSD1306_ShowReady();
}

/* -----------------------------------------------------------------------
 * Main application loop body
 * ----------------------------------------------------------------------- */
static void App_Loop(void)
{
    uint32_t now = HAL_GetTick();

    /* ================================================================
     * EXIT button — highest priority, opens relay immediately
     * (works regardless of current sys_state)
     * ================================================================ */
    if (btn_exit_flag) {
        btn_exit_flag = 0;
        if (sys_state == SYS_UNLOCKING) {
            /* Already open – just reset timer */
            state_tick = HAL_GetTick();
        } else {
            Enter_Unlocked(0, 1 /* from_exit_btn */);
        }
    }

    /* ================================================================
     * ENROLL button — only act when idle
     * ================================================================ */
    if (btn_enroll_flag) {
        btn_enroll_flag = 0;
        if (sys_state == SYS_IDLE) {
            sys_state  = SYS_ENROLLING;
            state_tick = HAL_GetTick();
            HAL_UART_Transmit(&huart1, (uint8_t *)"ENROLL\n", 7, 100);
            SSD1306_ShowEnrolling();
            DFPlayer_Play(DFP_TRACK_LOOK_AT_CAM);
        }
    }

    /* ================================================================
     * DELETE button — hold for 3 s to confirm
     * This uses a blocking loop but still receives UART via interrupts.
     * ================================================================ */
    if (btn_delete_flag) {
        btn_delete_flag = 0;
        if (sys_state == SYS_IDLE) {
            sys_state = SYS_DELETING;

            uint32_t hold_start   = HAL_GetTick();
            uint8_t  last_second  = 0xFF;
            uint8_t  confirmed    = 0;

            SSD1306_ShowHoldDelete(0);

            while (HAL_GPIO_ReadPin(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) == GPIO_PIN_RESET) {
                uint32_t held_ms = HAL_GetTick() - hold_start;
                uint8_t  sec     = (uint8_t)(held_ms / 1000U);

                /* Update progress display once per second */
                if (sec != last_second) {
                    last_second = sec;
                    SSD1306_ShowHoldDelete(sec);
                }

                if (held_ms >= DELETE_HOLD_MS) {
                    confirmed = 1;
                    break;
                }

                /* Process incoming UART bytes while waiting (non-blocking) */
                if (uart1_line_ready) {
                    uart1_line_ready = 0;
                    /* Ignore ESP32 messages during delete hold */
                }
            }

            if (confirmed) {
                SSD1306_ShowDeleting();
                HAL_UART_Transmit(&huart1, (uint8_t *)"DEL_ALL\n", 8, 100);
                /* Remain in SYS_DELETING – Parse_ESP32_Msg handles "DELETED" */
                state_tick = HAL_GetTick();
            } else {
                /* Released early – cancel */
                sys_state = SYS_IDLE;
                SSD1306_ShowReady();
            }
        }
    }

    /* ================================================================
     * Process completed UART line from ESP32-S3
     * ================================================================ */
    if (uart1_line_ready) {
        uart1_line_ready = 0;
        Parse_ESP32_Msg(uart1_line);
        now = HAL_GetTick(); /* Refresh time after potential state change */
    }

    /* ================================================================
     * State timeout handlers
     * ================================================================ */
    switch (sys_state) {

        case SYS_UNLOCKING:
            if ((now - state_tick) >= RELAY_OPEN_MS) {
                Relay_Close();
                sys_state = SYS_IDLE;
                SSD1306_ShowReady();
            }
            break;

        case SYS_DENIED:
            if ((now - state_tick) >= DENIED_DISPLAY_MS) {
                sys_state = SYS_IDLE;
                SSD1306_ShowReady();
            }
            break;

        case SYS_ENROLLING:
            if ((now - state_tick) >= ENROLL_TIMEOUT_MS) {
                /* ESP32 did not respond – cancel enrolment */
                sys_state       = SYS_IDLE;
                btn_enroll_flag = 0; /* Discard any button press queued during enrol */
                HAL_UART_Transmit(&huart1, (uint8_t *)"CANCEL\n", 7, 100);
                SSD1306_ShowReady();
            }
            break;

        case SYS_DELETING:
            /* "DELETED" from ESP32 is handled in Parse_ESP32_Msg.
             * Guard timeout in case ESP32 does not respond (10 s). */
            if ((now - state_tick) >= 10000U) {
                sys_state = SYS_IDLE;
                SSD1306_ShowReady();
            }
            break;

        default:
            break;
    }
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
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  App_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_Loop();
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
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1|RCC_PERIPHCLK_USART2
                              |RCC_PERIPHCLK_USART3|RCC_PERIPHCLK_I2C1;
  PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;
  PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
  PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
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
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

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
  huart3.Init.BaudRate = 9600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
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
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin : BTN_EXIT_Pin */
  GPIO_InitStruct.Pin = BTN_EXIT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(BTN_EXIT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : BTN_ENROLL_Pin BTN_DELETE_Pin */
  GPIO_InitStruct.Pin = BTN_ENROLL_Pin|BTN_DELETE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : LD2_Pin */
  GPIO_InitStruct.Pin = LD2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : RELAY_Pin */
  GPIO_InitStruct.Pin = RELAY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(RELAY_GPIO_Port, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI0_IRQn);

  HAL_NVIC_SetPriority(EXTI1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI1_IRQn);

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

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
