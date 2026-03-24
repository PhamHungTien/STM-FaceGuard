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
    SYS_CONNECTING, /* Waiting for ESP32 READY after boot                        */
    SYS_OFFLINE,    /* ESP32 not reachable; EXIT still local, admin ops blocked  */
    SYS_IDLE,
    SYS_UNLOCKING,
    SYS_ENROLLING,
    SYS_DELETING,
    SYS_DENIED,
    SYS_RESULT,     /* Showing ENROLLED/DELETED/DB_FULL; auto-returns after 3 s  */
    SYS_LOCKED      /* Security lockout: too many failed attempts                */
} SystemState_t;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define LOCKOUT_DISPLAY_MS  300000U  /* STM32-side lockout fallback: 5 minutes  */
#define RELAY_OPEN_MS         3000U  /* Door stays unlocked for 3 seconds       */
#define DELETE_HOLD_MS        3000U  /* Hold DELETE button 3s to confirm        */
#define DENIED_DISPLAY_MS     2500U  /* "Access Denied" shown for 2.5 seconds   */
#define ENROLL_TIMEOUT_MS    15000U  /* Enrolling mode auto-cancels after 15s   */
#define ENROLL_RETRY_MAX         2U  /* Retries if ENROLL command seems lost     */
#define CONNECT_TIMEOUT_MS   30000U  /* Fallback if ESP32 never sends READY     */
#define RESULT_DISPLAY_MS     3000U  /* ENROLLED/DELETED/DB_FULL display time   */
#define DEBOUNCE_MS            200U  /* Minimum ms between button events        */
#define MAX_ENROLLED_FACES_STM32 7U  /* Must match MAX_ENROLLED_FACES on ESP32  */
#define ESP32_SYNC_RETRY_MS   1000U  /* Periodic STATUS sync while link unknown  */

#define UART1_BUF_SIZE          64U  /* ESP32 message receive buffer            */
#define UART1_QUEUE_LEN          4U  /* Small queue for back-to-back UART lines */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart1_rx;

/* USER CODE BEGIN PV */
/* --- Button flags (set in ISR, cleared in main loop) --- */
static volatile uint8_t  btn_exit_flag   = 0;
static volatile uint8_t  btn_enroll_flag = 0;
static volatile uint8_t  btn_delete_flag = 0;
static volatile uint32_t btn_exit_tick   = 0;
static volatile uint32_t btn_enroll_tick = 0;
static volatile uint32_t btn_delete_tick = 0;
static volatile uint8_t  btn_exit_armed   = 0;
static volatile uint8_t  btn_enroll_armed = 0;
static volatile uint8_t  btn_delete_armed = 0;

/* --- UART1 receive buffer (ESP32-S3 → STM32) --- */
#define ESP32_DMA_RX_SIZE       64U
static uint8_t esp32_dma_rx_buf[ESP32_DMA_RX_SIZE];
static uint8_t  uart1_rx_byte    = 0;
static char     uart1_line[UART1_BUF_SIZE];
static uint16_t uart1_line_idx   = 0;
static char     uart1_queue[UART1_QUEUE_LEN][UART1_BUF_SIZE];
static volatile uint8_t  uart1_queue_head  = 0;
static volatile uint8_t  uart1_queue_tail  = 0;
static volatile uint32_t uart1_queue_drops = 0;
static volatile uint32_t uart1_error_count = 0; /* Counts HW errors — for diagnostics */

/* --- System state machine --- */
static SystemState_t sys_state       = SYS_IDLE;
static uint32_t      state_tick      = 0;
static uint32_t      esp32_sync_start_tick = 0;
static uint32_t      esp32_status_tick = 0;
static uint8_t       esp32_ready     = 0;

/* --- Enrolled face count (updated from ESP32 FACES/ENROLLED/DELETED msgs) --- */
static uint8_t       enrolled_faces  = 0;
static uint8_t       enroll_retry_count = 0;

/* --- DELETE hold state (non-blocking) --- */
static uint8_t       delete_hold_active = 0;
static uint32_t      delete_hold_start  = 0;
static uint8_t       delete_last_second = 0xFF;
/* --- Relay safety state --- */
static uint8_t       relay_open_active  = 0;
static uint32_t      relay_open_tick    = 0;
/* --- Lockout countdown --- */
static uint32_t      lockout_duration_ms   = LOCKOUT_DISPLAY_MS;
static uint32_t      lockout_last_second   = 0xFFFFFFFFU;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
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
static void Show_Ready(void);   /* Wrapper: shows ReadyFaces with enrolled_faces count */
static void Show_ESP32_LinkState(void);
static void Restore_LinkAwareIdle(void);
static uint8_t Button_IsPressed(GPIO_TypeDef *port, uint16_t pin);
static void Button_PollPress(volatile uint8_t *armed,
                             volatile uint8_t *flag,
                             volatile uint32_t *tick,
                             GPIO_TypeDef *port,
                             uint16_t pin,
                             uint32_t now);
static void Button_PollRelease(volatile uint8_t *armed,
                               volatile uint32_t *tick,
                               GPIO_TypeDef *port,
                               uint16_t pin,
                               uint32_t now);
static void Note_ESP32_Traffic(void);
static void Mark_ESP32_Alive(void);
static void Mark_ESP32_Offline(void);
static void ESP32_RequestStatus(uint8_t force);
static uint8_t UART1_DequeueLine(char *out);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* -----------------------------------------------------------------------
 * HAL UART RX Event callback – called when UART1 receives a full line or IDLE
 * ----------------------------------------------------------------------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        /* Ensure null-termination */
        if (Size < ESP32_DMA_RX_SIZE) {
            esp32_dma_rx_buf[Size] = '\0';
        } else {
            esp32_dma_rx_buf[ESP32_DMA_RX_SIZE - 1] = '\0';
        }

        /* Basic cleanup: remove \r or \n */
        char *ptr = (char *)esp32_dma_rx_buf;
        for (uint16_t i = 0; i < Size; i++) {
            if (ptr[i] == '\r' || ptr[i] == '\n') {
                ptr[i] = '\0';
                break;
            }
        }

        if (strlen(ptr) > 0) {
            /* Enqueue for main loop processing */
            uint8_t next_tail = (uint8_t)((uart1_queue_tail + 1U) % UART1_QUEUE_LEN);
            if (next_tail != uart1_queue_head) {
                strncpy(uart1_queue[uart1_queue_tail], ptr, UART1_BUF_SIZE - 1);
                uart1_queue[uart1_queue_tail][UART1_BUF_SIZE - 1] = '\0';
                uart1_queue_tail = next_tail;
            } else {
                uart1_queue_drops++;
            }
        }

        /* Restart DMA reception */
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* Keep for other UARTs if needed, USART1 now handled by RxEventCallback */
}

/* -----------------------------------------------------------------------
 * HAL UART Error callback – called when UART1 reports a hardware error
 * (overrun, framing, noise). Clears the error flag and re-arms reception
 * so the link to ESP32 recovers automatically without requiring a reset.
 * ----------------------------------------------------------------------- */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(huart);   /* Clear Overrun  */
        __HAL_UART_CLEAR_NEFLAG(huart);    /* Clear Noise    */
        __HAL_UART_CLEAR_FEFLAG(huart);    /* Clear Framing  */
        uart1_error_count++;               /* Track for diagnostics */
        
        /* Re-start DMA reception after error */
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
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
        if (btn_exit_armed &&
            Button_IsPressed(BTN_EXIT_GPIO_Port, BTN_EXIT_Pin) &&
            ((now - btn_exit_tick) >= DEBOUNCE_MS)) {
            btn_exit_tick  = now;
            btn_exit_armed = 0;
            btn_exit_flag  = 1;
        }
    } else if (GPIO_Pin == BTN_ENROLL_Pin) {
        if (btn_enroll_armed &&
            Button_IsPressed(BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin) &&
            ((now - btn_enroll_tick) >= DEBOUNCE_MS)) {
            btn_enroll_tick  = now;
            btn_enroll_armed = 0;
            btn_enroll_flag  = 1;
        }
    } else if (GPIO_Pin == BTN_DELETE_Pin) {
        if (btn_delete_armed &&
            Button_IsPressed(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) &&
            ((now - btn_delete_tick) >= DEBOUNCE_MS)) {
            btn_delete_tick  = now;
            btn_delete_armed = 0;
            btn_delete_flag  = 1;
        }
    }
}

/* ----------------------------------------------------------------------- */

static void Relay_Open(void)
{
    /* Relay module is active-HIGH: set PB0 HIGH to energise coil → open lock */
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(LD2_GPIO_Port,   LD2_Pin,   GPIO_PIN_SET);
    relay_open_active = 1;
    relay_open_tick   = HAL_GetTick();
}

static void Relay_Close(void)
{
    HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LD2_GPIO_Port,   LD2_Pin,   GPIO_PIN_RESET);
    relay_open_active = 0;
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

static void Show_Ready(void)
{
    SSD1306_ShowReadyFaces(enrolled_faces);
}

static void Show_ESP32_LinkState(void)
{
    if (esp32_ready) {
        Show_Ready();
    } else if (sys_state != SYS_OFFLINE &&
               ((HAL_GetTick() - esp32_sync_start_tick) < CONNECT_TIMEOUT_MS)) {
        SSD1306_ShowConnecting();
    } else {
        SSD1306_ShowESP32Offline();
    }
}

static uint8_t Button_IsPressed(GPIO_TypeDef *port, uint16_t pin)
{
    return (HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

static void Button_PollPress(volatile uint8_t *armed,
                             volatile uint8_t *flag,
                             volatile uint32_t *tick,
                             GPIO_TypeDef *port,
                             uint16_t pin,
                             uint32_t now)
{
    if (!*armed) {
        return;
    }

    if (!Button_IsPressed(port, pin)) {
        *tick = now;
        return;
    }

    if ((now - *tick) >= DEBOUNCE_MS) {
        *armed = 0;
        *flag = 1;
        *tick = now;
    }
}

static void Button_PollRelease(volatile uint8_t *armed,
                               volatile uint32_t *tick,
                               GPIO_TypeDef *port,
                               uint16_t pin,
                               uint32_t now)
{
    if (*armed) {
        return;
    }

    if (Button_IsPressed(port, pin)) {
        *tick = now;
        return;
    }

    if ((now - *tick) >= DEBOUNCE_MS) {
        *armed = 1;
        *tick = now;
    }
}

static void Note_ESP32_Traffic(void)
{
    esp32_status_tick = HAL_GetTick();
}

static void Restore_LinkAwareIdle(void)
{
    if (esp32_ready) {
        sys_state = SYS_IDLE;
        Show_Ready();
        return;
    }

    if ((HAL_GetTick() - esp32_sync_start_tick) < CONNECT_TIMEOUT_MS) {
        sys_state = SYS_CONNECTING;
        SSD1306_ShowConnecting();
    } else {
        Mark_ESP32_Offline();
    }
}

static void Mark_ESP32_Alive(void)
{
    esp32_ready = 1;
    Note_ESP32_Traffic();
}

static void Mark_ESP32_Offline(void)
{
    esp32_ready = 0;
    delete_hold_active = 0;
    sys_state = SYS_OFFLINE;
    SSD1306_ShowESP32Offline();
}

static void ESP32_RequestStatus(uint8_t force)
{
    uint32_t now = HAL_GetTick();

    if (!force && (now - esp32_status_tick) < ESP32_SYNC_RETRY_MS) {
        return;
    }

    esp32_status_tick = now;
    HAL_UART_Transmit(&huart1, (uint8_t *)"STATUS\n", 7, 100);
}

static uint8_t UART1_DequeueLine(char *out)
{
    uint8_t has_line = 0;

    __disable_irq();
    if (uart1_queue_head != uart1_queue_tail) {
        memcpy(out, uart1_queue[uart1_queue_head], UART1_BUF_SIZE);
        uart1_queue_head = (uint8_t)((uart1_queue_head + 1U) % UART1_QUEUE_LEN);
        has_line = 1;
    }
    __enable_irq();

    return has_line;
}

/* -----------------------------------------------------------------------
 * Parse a complete message line received from ESP32-S3.
 *
 * Protocol (ESP32 → STM32):
 *   "OPEN:<id>"       Face recognised, <id> = face database ID
 *   "DENIED"          Face not recognised (after voting + cooldown)
 *   "ENROLLED:<id>"   Enrolment success, new face ID
 *   "DELETED"         All faces cleared from database
 *   "READY"           ESP32 finished booting
 *   "FACES:<n>"       Face count update (sent right after READY)
 *   "DB_FULL"         Face DB at capacity, enrolment rejected
 *   "LOCKOUT"         Too many failures — security lockout started
 *   "LOCKOUT_CLEAR"   Lockout period expired — system back to normal
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
        if (strlen(msg) <= 5) return;          /* malformed: no ID field */
        Mark_ESP32_Alive();
        uint8_t id = (uint8_t)atoi(msg + 5);
        if (id >= MAX_ENROLLED_FACES_STM32) id = 0; /* clamp to valid range */
        Enter_Unlocked(id, 0);

    } else if (strcmp(msg, "DENIED") == 0) {
        Mark_ESP32_Alive();
        if (sys_state == SYS_IDLE) {
            Enter_Denied();
        }

    } else if (strncmp(msg, "ENROLLED:", 9) == 0) {
        if (strlen(msg) <= 9) return;          /* malformed: no ID field */
        Mark_ESP32_Alive();
        uint8_t id = (uint8_t)atoi(msg + 9);
        if (enrolled_faces < MAX_ENROLLED_FACES_STM32) enrolled_faces++;
        sys_state  = SYS_RESULT;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrolled(id, enrolled_faces, MAX_ENROLLED_FACES_STM32);
        DFPlayer_Play(DFP_TRACK_ENROLLED);

    } else if (strcmp(msg, "DELETED") == 0) {
        Mark_ESP32_Alive();
        enrolled_faces = 0;
        sys_state  = SYS_RESULT;
        state_tick = HAL_GetTick();
        SSD1306_ShowDeleted();
        DFPlayer_Play(DFP_TRACK_DELETED);

    } else if (strcmp(msg, "BOOTING") == 0) {
        esp32_ready = 0;
        delete_hold_active = 0;
        Note_ESP32_Traffic();
        if (sys_state != SYS_UNLOCKING) {
            sys_state = SYS_CONNECTING;
            SSD1306_ShowConnecting();
        }

    } else if (strncmp(msg, "CAM_FAIL:", 9) == 0) {
        esp32_ready = 0;
        delete_hold_active = 0;
        Note_ESP32_Traffic();
        Mark_ESP32_Offline();

    } else if (strcmp(msg, "READY") == 0) {
        Mark_ESP32_Alive();
        btn_enroll_flag = 0; /* Discard any buttons pressed during boot */
        btn_delete_flag = 0;
        btn_exit_flag   = 0;
        delete_hold_active = 0;
        if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE) {
            sys_state = SYS_IDLE;
            Show_Ready();
        }

    } else if (strncmp(msg, "FACES:", 6) == 0) {
        Mark_ESP32_Alive();
        /* Face count update from ESP32 (sent right after READY) */
        uint8_t n = (uint8_t)atoi(msg + 6);
        enrolled_faces = (n <= MAX_ENROLLED_FACES_STM32) ? n : MAX_ENROLLED_FACES_STM32;
        if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE) {
            sys_state = SYS_IDLE;
            delete_hold_active = 0;
        }
        if (sys_state == SYS_IDLE) {
            Show_Ready();
        }

    } else if (strcmp(msg, "DB_FULL") == 0) {
        Mark_ESP32_Alive();
        /* Enrollment rejected because face DB is at capacity */
        sys_state  = SYS_RESULT;
        state_tick = HAL_GetTick();
        SSD1306_ShowDbFull();
        DFPlayer_Play(DFP_TRACK_DENIED);

    /* --- Enrollment step guidance (5 poses, sent sequentially by ESP32) --- */
    } else if (strcmp(msg, "ENROLL_FRONT") == 0) {
        Mark_ESP32_Alive();
        enroll_retry_count = 0;
        sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick(); /* Reset enrol timeout each step */
        SSD1306_ShowEnrollStep(1, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_FRONT);

    } else if (strcmp(msg, "ENROLL_LEFT") == 0) {
        Mark_ESP32_Alive();
        enroll_retry_count = 0;
        sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(2, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_LEFT);

    } else if (strcmp(msg, "ENROLL_RIGHT") == 0) {
        Mark_ESP32_Alive();
        enroll_retry_count = 0;
        sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(3, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_RIGHT);

    } else if (strcmp(msg, "ENROLL_UP") == 0) {
        Mark_ESP32_Alive();
        enroll_retry_count = 0;
        sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(4, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_UP);

    } else if (strcmp(msg, "ENROLL_DOWN") == 0) {
        Mark_ESP32_Alive();
        enroll_retry_count = 0;
        sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(5, 5);
        DFPlayer_Play(DFP_TRACK_ENROLL_DOWN);

    } else if (strncmp(msg, "LOCKOUT:", 8) == 0 || strcmp(msg, "LOCKOUT") == 0) {
        Mark_ESP32_Alive();
        /* Security lockout: extract duration if present ("LOCKOUT:<ms>") */
        if (msg[7] == ':') {
            uint32_t dur = (uint32_t)strtoul(msg + 8, NULL, 10);
            lockout_duration_ms = (dur > 0U) ? dur : LOCKOUT_DISPLAY_MS;
        } else {
            lockout_duration_ms = LOCKOUT_DISPLAY_MS;
        }
        lockout_last_second = 0xFFFFFFFFU; /* force immediate countdown update */
        sys_state  = SYS_LOCKED;
        state_tick = HAL_GetTick();
        SSD1306_ShowLockout();
        DFPlayer_Play(DFP_TRACK_DENIED);

    } else if (strcmp(msg, "LOCKOUT_CLEAR") == 0) {
        Mark_ESP32_Alive();
        if (sys_state == SYS_LOCKED) {
            sys_state = SYS_IDLE;
            Show_Ready();
        }
    }
}

/* -----------------------------------------------------------------------
 * Application initialisation (called once after all HAL inits)
 * ----------------------------------------------------------------------- */
static void App_Init(void)
{
    uint32_t now = HAL_GetTick();

    /* Buttons configured correctly in CubeMX (Falling edge + Pull-up).
     * Clear any spurious EXTI flags that may have been set at boot. */
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_ENROLL_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_DELETE_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_EXIT_Pin);
    btn_enroll_flag = 0;
    btn_delete_flag = 0;
    btn_exit_flag   = 0;
    btn_exit_tick   = now;
    btn_enroll_tick = now;
    btn_delete_tick = now;
    btn_exit_armed   = Button_IsPressed(BTN_EXIT_GPIO_Port, BTN_EXIT_Pin) ? 0U : 1U;
    btn_enroll_armed = Button_IsPressed(BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin) ? 0U : 1U;
    btn_delete_armed = Button_IsPressed(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) ? 0U : 1U;
    esp32_ready     = 0;
    delete_hold_active = 0;
    uart1_line_idx  = 0;
    uart1_queue_head = 0;
    uart1_queue_tail = 0;
    esp32_sync_start_tick = HAL_GetTick();
    esp32_status_tick = 0;

    /* --- Enable USART1 NVIC (ESP32 receive) --- */
    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    /* --- Start UART1 DMA reception with Idle Line Detection --- */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT); // Disable half-transfer interrupt

    ESP32_RequestStatus(1);

    /* --- Enter CONNECTING state before other slow inits so READY/FACES
     * received during startup are not discarded. --- */
    sys_state  = SYS_CONNECTING;
    state_tick = HAL_GetTick();

    /* --- OLED initialisation and boot screen --- */
    SSD1306_Init();
    SSD1306_ShowBoot();
    SSD1306_ShowConnecting();

    /* --- DFPlayer initialisation (blocks ~3.7 s incl. reset + SD scan) --- */
    DFPlayer_Init();
}

/* -----------------------------------------------------------------------
 * Main application loop body
 * ----------------------------------------------------------------------- */
static void App_Loop(void)
{
    IWDG->KR = 0xAAAAU;         /* Feed watchdog — proves main loop is alive */
    uint32_t now = HAL_GetTick();
    char uart1_msg[UART1_BUF_SIZE];

    /* Relay failsafe: ensure door lock is re-energized after RELAY_OPEN_MS,
     * even if state changes unexpectedly while the relay is open. */
    if (relay_open_active && ((now - relay_open_tick) >= RELAY_OPEN_MS)) {
        Relay_Close();
        if (sys_state == SYS_UNLOCKING) {
            Restore_LinkAwareIdle();
        }
        now = HAL_GetTick();
    }

    Button_PollPress(&btn_enroll_armed, &btn_enroll_flag, &btn_enroll_tick,
                     BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin, now);
    Button_PollPress(&btn_delete_armed, &btn_delete_flag, &btn_delete_tick,
                     BTN_DELETE_GPIO_Port, BTN_DELETE_Pin, now);
    Button_PollRelease(&btn_exit_armed, &btn_exit_tick, BTN_EXIT_GPIO_Port, BTN_EXIT_Pin, now);
    Button_PollRelease(&btn_enroll_armed, &btn_enroll_tick, BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin, now);
    Button_PollRelease(&btn_delete_armed, &btn_delete_tick, BTN_DELETE_GPIO_Port, BTN_DELETE_Pin, now);

    /* ================================================================
     * EXIT button — highest priority, opens relay immediately
     * even if ESP32 is still connecting or currently locked out
     * ================================================================ */
    if (btn_exit_flag) {
        btn_exit_flag = 0;
        delete_hold_active = 0; /* EXIT takes priority over admin actions */
        if (sys_state == SYS_ENROLLING && esp32_ready) {
            HAL_UART_Transmit(&huart1, (uint8_t *)"CANCEL\n", 7, 100);
        }
        if (sys_state == SYS_UNLOCKING) {
            /* Already open – just reset timer */
            state_tick = HAL_GetTick();
            relay_open_tick = state_tick;
            relay_open_active = 1;
        } else {
            Enter_Unlocked(0, 1 /* from_exit_btn */);
        }
    }

    /* ================================================================
     * Process completed UART lines from ESP32-S3
     * Queue avoids losing READY/FACES when sent back-to-back at boot.
     * ================================================================ */
    while (UART1_DequeueLine(uart1_msg)) {
        Parse_ESP32_Msg(uart1_msg);
        now = HAL_GetTick(); /* Refresh time after potential state change */
    }

    if (!esp32_ready) {
        ESP32_RequestStatus(0);
    }

    /* ================================================================
     * DELETE hold confirmation — non-blocking so the main loop stays alive
     * ================================================================ */
    if (delete_hold_active) {
        uint32_t held_ms = now - delete_hold_start;
        uint8_t  sec     = (uint8_t)(held_ms / 1000U);

        /* Hard timeout: if button stuck >30 s, cancel to prevent lockout */
        if (held_ms > 30000U) {
            delete_hold_active = 0;
            sys_state = SYS_IDLE;
            Show_Ready();
        }

        else if (HAL_GPIO_ReadPin(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) != GPIO_PIN_RESET) {
            delete_hold_active = 0;
            sys_state = SYS_IDLE;
            Show_Ready();
        } else {
            if (sec != delete_last_second) {
                delete_last_second = sec;
                SSD1306_ShowHoldDelete(sec);
            }

            if (held_ms >= DELETE_HOLD_MS) {
                delete_hold_active = 0;
                SSD1306_ShowDeleting();
                if (HAL_UART_Transmit(&huart1, (uint8_t *)"DEL_ALL\n", 8, 200) == HAL_OK) {
                    state_tick = now;
                } else {
                    /* TX failed — treat as link loss and go offline */
                    Mark_ESP32_Offline();
                }
            }
        }
    }

    /* ================================================================
     * ENROLL button — only act when idle
     * ================================================================ */
    if (btn_enroll_flag) {
        btn_enroll_flag = 0;
        if (sys_state == SYS_IDLE && esp32_ready) {
            enroll_retry_count = 0;
            sys_state  = SYS_ENROLLING;
            state_tick = HAL_GetTick();
            HAL_UART_Transmit(&huart1, (uint8_t *)"ENROLL\n", 7, 100);
            SSD1306_ShowEnrolling();
            /* Audio is driven by ESP32's ENROLL_FRONT response to avoid double-play */
        } else if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE || !esp32_ready) {
            ESP32_RequestStatus(1);
            Show_ESP32_LinkState();
        }
    }

    /* ================================================================
     * DELETE button — hold for 3 s to confirm
     * Hold detection is non-blocking so UART/state handling still runs.
     * ================================================================ */
    if (btn_delete_flag) {
        btn_delete_flag = 0;
        if (sys_state == SYS_IDLE && esp32_ready && !delete_hold_active) {
            sys_state = SYS_DELETING;
            delete_hold_active = 1;
            delete_hold_start  = now;
            delete_last_second = 0xFF;
            SSD1306_ShowHoldDelete(0);
        } else if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE || !esp32_ready) {
            ESP32_RequestStatus(1);
            Show_ESP32_LinkState();
        }
    }

    /* ================================================================
     * State timeout handlers
     * ================================================================ */
    switch (sys_state) {

        case SYS_CONNECTING:
            /* If ESP32 never sends READY/FACES, show an explicit offline state. */
            if ((now - state_tick) >= CONNECT_TIMEOUT_MS) {
                Mark_ESP32_Offline();
            }
            break;

        case SYS_UNLOCKING:
            if (!relay_open_active) {
                Restore_LinkAwareIdle();
            }
            break;

        case SYS_DENIED:
            if ((now - state_tick) >= DENIED_DISPLAY_MS) {
                Restore_LinkAwareIdle();
            }
            break;

        case SYS_ENROLLING:
            if ((now - state_tick) >= ENROLL_TIMEOUT_MS) {
                /* Retry: send CANCEL first so ESP32 resets its enrollment state,
                 * then re-send ENROLL to restart from step 0. */
                if (esp32_ready && (enroll_retry_count < ENROLL_RETRY_MAX)) {
                    enroll_retry_count++;
                    state_tick = now;
                    HAL_UART_Transmit(&huart1, (uint8_t *)"CANCEL\n", 7, 100);
                    HAL_UART_Transmit(&huart1, (uint8_t *)"ENROLL\n", 7, 100);
                    SSD1306_ShowEnrolling();
                    break;
                }

                btn_enroll_flag = 0; /* Discard queued presses during enrol timeout */
                HAL_UART_Transmit(&huart1, (uint8_t *)"CANCEL\n", 7, 100);
                enroll_retry_count = 0;
                /* Brief failure notification via RESULT state (non-blocking) */
                sys_state  = SYS_RESULT;
                state_tick = HAL_GetTick();
                SSD1306_ShowDenied();
                DFPlayer_Play(DFP_TRACK_DENIED);
                ESP32_RequestStatus(1);
                break;
            }
            break;

        case SYS_DELETING:
            /* "DELETED" from ESP32 is handled in Parse_ESP32_Msg.
             * Guard timeout only after DEL_ALL was actually sent. */
            if (!delete_hold_active && (now - state_tick) >= 10000U) {
                Mark_ESP32_Offline();
                ESP32_RequestStatus(1);
            }
            break;

        case SYS_RESULT:
            /* Auto-return to Ready after showing ENROLLED / DELETED / DB_FULL */
            if ((now - state_tick) >= RESULT_DISPLAY_MS) {
                Restore_LinkAwareIdle();
            }
            break;

        case SYS_LOCKED: {
            uint32_t elapsed_ms = now - state_tick;
            /* STM32-side fallback: if ESP32 never sends LOCKOUT_CLEAR,
             * unlock after lockout_duration_ms to prevent permanent deadlock. */
            if (elapsed_ms >= lockout_duration_ms) {
                lockout_last_second = 0xFFFFFFFFU;
                Restore_LinkAwareIdle();
            } else {
                /* Update countdown display once per second */
                uint32_t remaining_s = (lockout_duration_ms - elapsed_ms + 999U) / 1000U;
                if (remaining_s != lockout_last_second) {
                    lockout_last_second = remaining_s;
                    SSD1306_ShowLockedCountdown(remaining_s);
                }
            }
            break;
        }

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
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */
  App_Init();

  /* ── Independent Watchdog (IWDG) ──────────────────────────────────────────
   * Started AFTER App_Init so long HAL_Delay calls during DFPlayer init do
   * not trigger a spurious reset.
   * LSI ≈ 40 kHz, prescaler = 64 (/64), reload = 3124 → timeout ≈ 5.0 s.
   * Feed in App_Loop (every iteration) and inside the DELETE hold loop.
   * If the main loop ever hangs for > 5 s the MCU resets automatically.
   * ─────────────────────────────────────────────────────────────────────── */
  IWDG->KR  = 0xCCCCU;           /* Enable IWDG                            */
  IWDG->KR  = 0x5555U;           /* Unlock PR / RLR registers              */
  IWDG->PR  = IWDG_PR_PR_2;      /* Prescaler = 64                         */
  IWDG->RLR = 3124U;             /* Reload → 3124 × (64/40000) ≈ 5.0 s    */
  while (IWDG->SR != 0U) {}      /* Wait for PR/RLR to be updated in HW    */
  IWDG->KR  = 0xAAAAU;           /* Reload counter (arm watchdog)          */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    App_Loop();
    /* Low-power sleep: enter only when no work is pending.
     * Double-check pattern: disable IRQs, re-test flags, then __WFI.
     * This prevents sleeping if an ISR set a flag between the App_Loop
     * check and the __WFI instruction. SysTick (1 ms) wakes the CPU
     * in the worst case, so timeouts remain accurate. */
    __disable_irq();
    if ((uart1_queue_head == uart1_queue_tail) && !btn_exit_flag &&
        !btn_enroll_flag  && !btn_delete_flag) {
        __WFI();           /* CPU sleeps; any IRQ (including SysTick) wakes it */
    }
    __enable_irq();
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
  /* Override CubeMX default (0x0010020A): use 100 kHz Standard Mode at HSI 8 MHz.
   * PRESC=1  SCLDEL=4  SDADEL=2  SCLH=15  SCLL=19 → 0x10420F13
   * Provides comfortable margins for breadboard capacitive loads. */
  hi2c1.Init.Timing = 0x10420F13;
  HAL_I2C_Init(&hi2c1);
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
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel5_IRQn);

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
