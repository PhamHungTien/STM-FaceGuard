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
#define DENIED_DISPLAY_MS     1500U  /* "Access Denied" shown for 1.5 seconds   */
#define ENROLL_TIMEOUT_MS    15000U  /* Enrolling mode auto-cancels after 15s   */
#define ENROLL_RETRY_MAX         2U  /* Retries if ENROLL command seems lost     */
#define CONNECT_TIMEOUT_MS   30000U  /* Fallback if ESP32 never sends READY     */
#define RESULT_DISPLAY_MS     2000U  /* ENROLLED/DELETED/DB_FULL display time   */
#define DEBOUNCE_MS            200U  /* Minimum ms between button events        */
#define MAX_ENROLLED_FACES_STM32 7U  /* Must match MAX_ENROLLED_FACES on ESP32  */
#define ESP32_SYNC_RETRY_MS   1000U  /* Periodic STATUS sync while link unknown  */
#define ESP32_LINK_WARN_MS   10000U  /* Start probing if no ESP32 traffic        */
#define ESP32_LINK_LOSS_MS   20000U  /* Confirmed offline after repeated silence */
#define ESP32_SOFT_RESET_DELAY_MS 30000U
#define ESP32_HARD_RESET_DELAY_MS 45000U
#define ESP32_SOFT_RESET_MAX      2U
#define ESP32_HARD_RESET_MAX      1U
#define ESP32_RESET_PULSE_MS    150U
#define UART_SECURE_HELLO_RETRY_MS 3000U
#define UART_LINK_PACKET_BUF_SIZE  96U
#define UART_LINK_CRYPT_MAX_LEN    32U

#define UART1_BUF_SIZE          96U  /* ESP32 message receive buffer            */
#define UART1_QUEUE_LEN          8U  /* Queue multiple ESP32 lines per DMA burst */
#define MAX_ENROLL_STEPS         5U
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;
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
#define ESP32_DMA_RX_SIZE      256U
static uint8_t esp32_dma_rx_buf[ESP32_DMA_RX_SIZE];
static char     uart1_queue[UART1_QUEUE_LEN][UART1_BUF_SIZE];
static volatile uint8_t  uart1_queue_head  = 0;
static volatile uint8_t  uart1_queue_tail  = 0;
static volatile uint32_t uart1_queue_drops = 0;
static volatile uint32_t uart1_error_count = 0;

/* --- System state machine --- */
static SystemState_t sys_state       = SYS_IDLE;
static uint32_t      state_tick      = 0;
static uint32_t      esp32_sync_start_tick = 0;
static uint32_t      esp32_status_tick = 0;
static uint32_t      esp32_last_rx_tick = 0;
static uint32_t      esp32_offline_tick = 0;
static uint32_t      uart_secure_hello_tick = 0;
static uint32_t      esp32_probe_tick = 0;
static uint8_t       esp32_ready     = 0;
static uint8_t       esp32_soft_reset_count = 0;
static uint8_t       esp32_hard_reset_count = 0;

/* --- UART link hardening state --- */
static uint8_t       uart_secure_active = 0;
static uint8_t       uart_secure_tx_seq = 1U;
static uint8_t       uart_secure_rx_seq = 0U;
static uint8_t       uart_secure_rx_synced = 0U;

/* --- Enrolled face count (updated from ESP32 FACES/ENROLLED/DELETED msgs) --- */
static uint8_t       enrolled_faces  = 0;
static uint8_t       enroll_retry_count = 0;
static uint8_t       enroll_total_steps = 1U;

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
static void MX_USART1_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART6_UART_Init(void);
/* USER CODE BEGIN PFP */
static void App_Init(void);
static void App_Loop(void);
static void Relay_Open(void);
static void Relay_Close(void);
static void Enter_Unlocked(uint8_t face_id, uint8_t from_exit_btn);
static void Enter_Denied(void);
static void Parse_ESP32_Msg(const char *msg);
static void Show_Ready(void);
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
static void ESP32_TryRecover(uint32_t now);
static void ESP32_HardResetPulse(void);
static void UART1_EnqueueLine(const char *line, uint16_t len);
static uint8_t UART1_DequeueLine(char *out);
static uint8_t Enroll_TotalForDisplay(uint8_t step);
static HAL_StatusTypeDef UART_SendCommand(const char *plain, uint8_t force_plain);
static void UART_RequestSecureHello(uint8_t force);
static void UART_EnableSecure(void);
static uint8_t UART_DecodeLine(const char *line, char *out, uint16_t out_size);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* -----------------------------------------------------------------------
 * HAL UART RX Event callback – called when UART1 receives a full line or IDLE
 * ----------------------------------------------------------------------- */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1) {
        uint16_t rx_len = (Size < ESP32_DMA_RX_SIZE) ? Size : (ESP32_DMA_RX_SIZE - 1U);
        char *ptr = (char *)esp32_dma_rx_buf;
        uint16_t line_start = 0U;

        ptr[rx_len] = '\0';

        for (uint16_t i = 0; i < rx_len; i++) {
            if (ptr[i] == '\r' || ptr[i] == '\n') {
                if (i > line_start) {
                    UART1_EnqueueLine(&ptr[line_start], (uint16_t)(i - line_start));
                }
                line_start = (uint16_t)(i + 1U);
            }
        }

        if (rx_len > line_start) {
            UART1_EnqueueLine(&ptr[line_start], (uint16_t)(rx_len - line_start));
        }

        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    /* Keep for other UARTs if needed, USART1 now handled by RxEventCallback */
}

/* -----------------------------------------------------------------------
 * HAL UART Error callback
 * ----------------------------------------------------------------------- */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        uart1_error_count++;

        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
}

/* -----------------------------------------------------------------------
 * HAL GPIO EXTI callback – button debouncing
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

static const uint32_t uart_link_key_enc[4] = {
    0x81B4D3A7UL, 0x6F128C5EUL, 0x27E9B041UL, 0xC35A719DUL
};

static const uint32_t uart_link_key_mac[4] = {
    0x14C2A96BUL, 0x8D37F050UL, 0x53AE1C84UL, 0xF60B4271UL
};

static uint32_t UART_LinkReadBE32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8)  |
           (uint32_t)src[3];
}

static void UART_LinkWriteBE32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static char UART_LinkHexDigit(uint8_t value)
{
    value &= 0x0FU;
    return (value < 10U) ? (char)('0' + value) : (char)('A' + (value - 10U));
}

static int8_t UART_LinkParseHex(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}

static void UART_LinkBytesToHex(const uint8_t *src, uint16_t len, char *dst)
{
    uint16_t i;

    for (i = 0U; i < len; i++) {
        dst[(uint16_t)(i * 2U)]     = UART_LinkHexDigit((uint8_t)(src[i] >> 4));
        dst[(uint16_t)(i * 2U + 1U)] = UART_LinkHexDigit(src[i]);
    }
    dst[(uint16_t)(len * 2U)] = '\0';
}

static uint8_t UART_LinkHexToBytes(const char *src, uint16_t hex_len, uint8_t *dst)
{
    uint16_t i;

    if ((hex_len == 0U) || ((hex_len & 1U) != 0U)) return 0U;

    for (i = 0U; i < hex_len; i += 2U) {
        int8_t hi = UART_LinkParseHex(src[i]);
        int8_t lo = UART_LinkParseHex(src[i + 1U]);

        if (hi < 0 || lo < 0) return 0U;
        dst[i / 2U] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    }

    return 1U;
}

static void UART_LinkXteaEncrypt(uint32_t *v0, uint32_t *v1, const uint32_t key[4])
{
    uint32_t a = *v0;
    uint32_t b = *v1;
    uint32_t sum = 0U;
    uint8_t round;

    for (round = 0U; round < 32U; round++) {
        a += (((b << 4) ^ (b >> 5)) + b) ^ (sum + key[sum & 3U]);
        sum += 0x9E3779B9UL;
        b += (((a << 4) ^ (a >> 5)) + a) ^ (sum + key[(sum >> 11) & 3U]);
    }

    *v0 = a;
    *v1 = b;
}

static void UART_LinkCrypt(uint8_t seq, const uint8_t *in, uint16_t len, uint8_t *out)
{
    uint16_t offset = 0U;
    uint8_t block = 0U;

    while (offset < len) {
        uint32_t v0 = 0x53544647UL ^ ((uint32_t)seq << 24) ^ block;
        uint32_t v1 = 0x4C4E4B31UL ^ ((uint32_t)len << 16) ^ 0x9E3779B9UL;
        uint8_t keystream[8];
        uint16_t i;
        uint16_t chunk = (uint16_t)(len - offset);

        if (chunk > 8U) chunk = 8U;

        UART_LinkXteaEncrypt(&v0, &v1, uart_link_key_enc);
        UART_LinkWriteBE32(&keystream[0], v0);
        UART_LinkWriteBE32(&keystream[4], v1);

        for (i = 0U; i < chunk; i++) {
            out[offset + i] = (uint8_t)(in[offset + i] ^ keystream[i]);
        }

        offset = (uint16_t)(offset + chunk);
        block++;
    }
}

static uint64_t UART_LinkMac(uint8_t seq, const uint8_t *data, uint16_t len)
{
    uint32_t v0 = 0x4D414331UL ^ (uint32_t)seq;
    uint32_t v1 = 0x53544647UL ^ (uint32_t)len;
    uint16_t offset = 0U;

    UART_LinkXteaEncrypt(&v0, &v1, uart_link_key_mac);

    while (offset < len) {
        uint8_t block[8] = {0};
        uint16_t chunk = (uint16_t)(len - offset);

        if (chunk > 8U) chunk = 8U;
        memcpy(block, &data[offset], chunk);
        v0 ^= UART_LinkReadBE32(&block[0]);
        v1 ^= UART_LinkReadBE32(&block[4]);
        UART_LinkXteaEncrypt(&v0, &v1, uart_link_key_mac);
        offset = (uint16_t)(offset + chunk);
    }

    return (((uint64_t)v0) << 32) | (uint64_t)v1;
}

static uint8_t UART_LinkShouldResync(uint8_t seq, const char *plain)
{
    if (seq != 1U) return 0U;

    return (uint8_t)(
        (strcmp(plain, "BOOTING") == 0) ||
        (strcmp(plain, "READY") == 0) ||
        (strcmp(plain, "SECURE_READY") == 0) ||
        (strcmp(plain, "SECURE_HELLO") == 0)
    );
}

static uint8_t UART_LinkAcceptSeq(uint8_t seq, const char *plain)
{
    if (!uart_secure_rx_synced) {
        uart_secure_rx_synced = 1U;
        uart_secure_rx_seq = seq;
        return 1U;
    }

    if (seq == uart_secure_rx_seq) {
        if (UART_LinkShouldResync(seq, plain)) {
            uart_secure_rx_seq = seq;
            return 1U;
        }
        return 0U;
    }

    if (UART_LinkShouldResync(seq, plain)) {
        uart_secure_rx_seq = seq;
        return 1U;
    }

    if ((uint8_t)(seq - uart_secure_rx_seq) <= 128U) {
        uart_secure_rx_seq = seq;
        return 1U;
    }

    return 0U;
}

static void UART_EnableSecure(void)
{
    uart_secure_active = 1U;
}

static HAL_StatusTypeDef UART_SendCommand(const char *plain, uint8_t force_plain)
{
    size_t plain_len = strlen(plain);
    char packet[UART_LINK_PACKET_BUF_SIZE];

    if (plain_len == 0U) return HAL_ERROR;

    if (!force_plain && uart_secure_active) {
        uint8_t seq = uart_secure_tx_seq;
        uint8_t cipher[UART_LINK_CRYPT_MAX_LEN];
        char cipher_hex[(UART_LINK_CRYPT_MAX_LEN * 2U) + 1U];
        uint64_t tag;
        int written;

        if (plain_len > UART_LINK_CRYPT_MAX_LEN) return HAL_ERROR;
        if (seq == 0U) seq = 1U;
        uart_secure_tx_seq = (uint8_t)(seq + 1U);
        if (uart_secure_tx_seq == 0U) uart_secure_tx_seq = 1U;

        UART_LinkCrypt(seq, (const uint8_t *)plain, (uint16_t)plain_len, cipher);
        UART_LinkBytesToHex(cipher, (uint16_t)plain_len, cipher_hex);
        tag = UART_LinkMac(seq, cipher, (uint16_t)plain_len);

        written = snprintf(packet, sizeof(packet),
                           "!%02X:%s:%08lX%08lX\n",
                           seq,
                           cipher_hex,
                           (unsigned long)(tag >> 32),
                           (unsigned long)(tag & 0xFFFFFFFFUL));
        if (written <= 0 || (size_t)written >= sizeof(packet)) return HAL_ERROR;
        return HAL_UART_Transmit(&huart1, (uint8_t *)packet, (uint16_t)written, 100);
    }

    if ((plain_len + 1U) >= sizeof(packet)) return HAL_ERROR;
    memcpy(packet, plain, plain_len);
    packet[plain_len] = '\n';
    packet[plain_len + 1U] = '\0';
    return HAL_UART_Transmit(&huart1, (uint8_t *)packet, (uint16_t)(plain_len + 1U), 100);
}

static void UART_RequestSecureHello(uint8_t force)
{
    uint32_t now = HAL_GetTick();

    if (uart_secure_active) return;
    if (!force && ((now - uart_secure_hello_tick) < UART_SECURE_HELLO_RETRY_MS)) return;

    uart_secure_hello_tick = now;
    (void)UART_SendCommand("SECURE_HELLO", 1U);
}

static uint8_t UART_DecodeLine(const char *line, char *out, uint16_t out_size)
{
    size_t line_len = strlen(line);

    if (line_len == 0U || out_size == 0U) return 0U;

    if (line[0] != '!') {
        size_t copy_len = (line_len < (size_t)(out_size - 1U)) ? line_len : (size_t)(out_size - 1U);
        memcpy(out, line, copy_len);
        out[copy_len] = '\0';
        return 1U;
    }

    if (line_len < 22U || line[3] != ':') return 0U;

    {
        int8_t hi = UART_LinkParseHex(line[1]);
        int8_t lo = UART_LinkParseHex(line[2]);
        const char *tag_sep = strrchr(line, ':');
        uint8_t seq;
        uint16_t cipher_hex_len;
        uint16_t cipher_len;
        uint8_t cipher[UART_LINK_CRYPT_MAX_LEN];
        uint8_t plain[UART_LINK_CRYPT_MAX_LEN + 1U];
        uint64_t rx_tag = 0U;
        uint64_t calc_tag;
        uint8_t i;

        if (hi < 0 || lo < 0 || tag_sep == NULL || tag_sep <= (line + 4)) return 0U;
        if ((size_t)(line_len - (size_t)(tag_sep - line) - 1U) != 16U) return 0U;

        seq = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
        cipher_hex_len = (uint16_t)(tag_sep - (line + 4));
        cipher_len = (uint16_t)(cipher_hex_len / 2U);

        if (cipher_len == 0U || cipher_len >= out_size || cipher_len > UART_LINK_CRYPT_MAX_LEN) return 0U;
        if (!UART_LinkHexToBytes(line + 4, cipher_hex_len, cipher)) return 0U;

        for (i = 0U; i < 16U; i++) {
            int8_t nibble = UART_LinkParseHex(tag_sep[1 + i]);
            if (nibble < 0) return 0U;
            rx_tag = (rx_tag << 4) | (uint64_t)(uint8_t)nibble;
        }

        calc_tag = UART_LinkMac(seq, cipher, cipher_len);
        if (calc_tag != rx_tag) return 0U;

        UART_LinkCrypt(seq, cipher, cipher_len, plain);
        plain[cipher_len] = '\0';

        if (!UART_LinkAcceptSeq(seq, (const char *)plain)) return 0U;

        memcpy(out, plain, cipher_len + 1U);
        UART_EnableSecure();
        return 1U;
    }
}

static void Relay_Open(void)
{
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
    if (!*armed) return;
    if (!Button_IsPressed(port, pin)) { *tick = now; return; }
    if ((now - *tick) >= DEBOUNCE_MS) { *armed = 0; *flag = 1; *tick = now; }
}

static void Button_PollRelease(volatile uint8_t *armed,
                               volatile uint32_t *tick,
                               GPIO_TypeDef *port,
                               uint16_t pin,
                               uint32_t now)
{
    if (*armed) return;
    if (Button_IsPressed(port, pin)) { *tick = now; return; }
    if ((now - *tick) >= DEBOUNCE_MS) { *armed = 1; *tick = now; }
}

static void Note_ESP32_Traffic(void)
{
    esp32_last_rx_tick = HAL_GetTick();
    esp32_probe_tick = 0U;
}

static void Restore_LinkAwareIdle(void)
{
    if (esp32_ready) { sys_state = SYS_IDLE; Show_Ready(); return; }
    if ((HAL_GetTick() - esp32_sync_start_tick) < CONNECT_TIMEOUT_MS) {
        sys_state = SYS_CONNECTING; SSD1306_ShowConnecting();
    } else {
        Mark_ESP32_Offline();
    }
}

static void Mark_ESP32_Alive(void)
{
    esp32_ready = 1U;
    esp32_offline_tick = 0U;
    esp32_soft_reset_count = 0U;
    esp32_hard_reset_count = 0U;
    Note_ESP32_Traffic();
}

static void Mark_ESP32_Offline(void)
{
    if (esp32_offline_tick == 0U) {
        esp32_offline_tick = HAL_GetTick();
    }
    esp32_ready = 0U;
    delete_hold_active = 0U;
    sys_state = SYS_OFFLINE;
    SSD1306_ShowESP32Offline();
}

static void ESP32_RequestStatus(uint8_t force)
{
    uint32_t now = HAL_GetTick();
    if (!force && (now - esp32_status_tick) < ESP32_SYNC_RETRY_MS) return;
    esp32_status_tick = now;
    (void)UART_SendCommand("STATUS", 0U);
}

static void ESP32_HardResetPulse(void)
{
    HAL_GPIO_WritePin(ESP32_RST_GPIO_Port, ESP32_RST_Pin, GPIO_PIN_RESET);
    HAL_Delay(ESP32_RESET_PULSE_MS);
    HAL_GPIO_WritePin(ESP32_RST_GPIO_Port, ESP32_RST_Pin, GPIO_PIN_SET);
}

static void ESP32_TryRecover(uint32_t now)
{
    uint32_t offline_ms;

    if (esp32_ready || esp32_offline_tick == 0U) return;

    offline_ms = now - esp32_offline_tick;

    if ((esp32_soft_reset_count < ESP32_SOFT_RESET_MAX) &&
        (offline_ms >= ESP32_SOFT_RESET_DELAY_MS)) {
        if (UART_SendCommand("REBOOT", 0U) == HAL_OK) {
            esp32_soft_reset_count++;
            esp32_offline_tick = now;
            esp32_sync_start_tick = now;
            sys_state = SYS_CONNECTING;
            state_tick = now;
            SSD1306_ShowConnecting();
        }
        return;
    }

    if ((esp32_hard_reset_count < ESP32_HARD_RESET_MAX) &&
        (offline_ms >= ESP32_HARD_RESET_DELAY_MS)) {
        ESP32_HardResetPulse();
        esp32_hard_reset_count++;
        esp32_offline_tick = HAL_GetTick();
        esp32_sync_start_tick = esp32_offline_tick;
        sys_state = SYS_CONNECTING;
        state_tick = esp32_offline_tick;
        SSD1306_ShowConnecting();
    }
}

static void UART1_EnqueueLine(const char *line, uint16_t len)
{
    uint8_t next_tail;
    uint16_t copy_len;

    if (len == 0U) return;

    next_tail = (uint8_t)((uart1_queue_tail + 1U) % UART1_QUEUE_LEN);
    if (next_tail == uart1_queue_head) {
        uart1_queue_drops++;
        return;
    }

    copy_len = (len < (UART1_BUF_SIZE - 1U)) ? len : (UART1_BUF_SIZE - 1U);
    memcpy(uart1_queue[uart1_queue_tail], line, copy_len);
    uart1_queue[uart1_queue_tail][copy_len] = '\0';
    uart1_queue_tail = next_tail;
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

static uint8_t Enroll_TotalForDisplay(uint8_t step)
{
    uint8_t total = enroll_total_steps;

    if (total < 1U) total = 1U;
    if (total > MAX_ENROLL_STEPS) total = MAX_ENROLL_STEPS;
    if (step > total) total = step;

    return total;
}

/* -----------------------------------------------------------------------
 * Parse a complete message line received from ESP32-S3
 * ----------------------------------------------------------------------- */
static void Parse_ESP32_Msg(const char *msg)
{
    if (strcmp(msg, "SECURE_READY") == 0) {
        UART_EnableSecure();
        Note_ESP32_Traffic();

    } else if (strcmp(msg, "SECURE_HELLO") == 0) {
        UART_EnableSecure();
        Note_ESP32_Traffic();
        (void)UART_SendCommand("SECURE_READY", 1U);

    } else if (strncmp(msg, "OPEN:", 5) == 0) {
        if (strlen(msg) <= 5) return;
        Mark_ESP32_Alive();
        uint8_t id = (uint8_t)atoi(msg + 5);
        if (id >= MAX_ENROLLED_FACES_STM32) id = 0;
        Enter_Unlocked(id, 0);

    } else if (strcmp(msg, "DENIED") == 0) {
        Mark_ESP32_Alive();
        if (sys_state == SYS_IDLE) Enter_Denied();

    } else if (strncmp(msg, "ENROLLED:", 9) == 0) {
        if (strlen(msg) <= 9) return;
        Mark_ESP32_Alive();
        uint8_t id = (uint8_t)atoi(msg + 9);
        if (enrolled_faces < MAX_ENROLLED_FACES_STM32) enrolled_faces++;
        sys_state  = SYS_RESULT; state_tick = HAL_GetTick();
        SSD1306_ShowEnrolled(id, enrolled_faces, MAX_ENROLLED_FACES_STM32);
        DFPlayer_Play(DFP_TRACK_ENROLLED);

    } else if (strcmp(msg, "DELETED") == 0) {
        Mark_ESP32_Alive();
        enrolled_faces = 0; sys_state = SYS_RESULT; state_tick = HAL_GetTick();
        SSD1306_ShowDeleted(); DFPlayer_Play(DFP_TRACK_DELETED);

    } else if (strcmp(msg, "BOOTING") == 0) {
        esp32_ready = 0; delete_hold_active = 0; Note_ESP32_Traffic();
        esp32_offline_tick = 0U;
        /* Reset secure link so re-handshake starts cleanly after ESP32 restart.
         * Without this, STM32 would keep sending encrypted STATUS to a freshly
         * booted ESP32 that has not yet enabled secure mode. */
        uart_secure_active    = 0U;
        uart_secure_tx_seq    = 1U;
        uart_secure_rx_seq    = 0U;
        uart_secure_rx_synced = 0U;
        uart_secure_hello_tick = 0U;
        if (sys_state != SYS_UNLOCKING) {
            sys_state = SYS_CONNECTING;
            state_tick = HAL_GetTick();
            esp32_sync_start_tick = state_tick;
            SSD1306_ShowConnecting();
        }
        UART_RequestSecureHello(1U); /* Re-initiate handshake immediately */

    } else if (strncmp(msg, "CAM_FAIL:", 9) == 0) {
        /* ESP32 alive but camera not ready — keep link, let recovery timeout handle reset */
        esp32_ready = 0; delete_hold_active = 0; Note_ESP32_Traffic();
        if (esp32_offline_tick == 0U) esp32_offline_tick = HAL_GetTick();

    } else if (strcmp(msg, "READY") == 0) {
        Mark_ESP32_Alive();
        if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE) {
            /* Discard stale button presses that accumulated while offline/connecting */
            btn_enroll_flag = 0; btn_delete_flag = 0; btn_exit_flag = 0;
            delete_hold_active = 0;
            sys_state = SYS_IDLE; Show_Ready();
        }

    } else if (strncmp(msg, "FACES:", 6) == 0) {
        Mark_ESP32_Alive();
        uint8_t n = (uint8_t)atoi(msg + 6);
        uint8_t new_count = (n <= MAX_ENROLLED_FACES_STM32) ? n : MAX_ENROLLED_FACES_STM32;
        uint8_t was_not_idle = (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE) ? 1U : 0U;
        if (was_not_idle) {
            sys_state = SYS_IDLE; delete_hold_active = 0;
        }
        /* Only redraw when transitioning to IDLE or when count actually changed.
         * Avoids rewriting the OLED on every 2-second beacon (which caused flicker). */
        if (sys_state == SYS_IDLE && (was_not_idle || new_count != enrolled_faces)) {
            enrolled_faces = new_count;
            Show_Ready();
        } else {
            enrolled_faces = new_count;
        }

    } else if (strncmp(msg, "ENROLL_CFG:", 11) == 0) {
        Mark_ESP32_Alive();
        uint8_t steps = (uint8_t)atoi(msg + 11);
        if (steps < 1U) steps = 1U;
        if (steps > MAX_ENROLL_STEPS) steps = MAX_ENROLL_STEPS;
        enroll_total_steps = steps;

    } else if (strcmp(msg, "DB_FULL") == 0) {
        Mark_ESP32_Alive();
        sys_state = SYS_RESULT; state_tick = HAL_GetTick();
        SSD1306_ShowDbFull(); DFPlayer_Play(DFP_TRACK_DENIED);

    } else if (strcmp(msg, "ENROLL_FRONT") == 0) {
        Mark_ESP32_Alive(); enroll_retry_count = 0; sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(1, Enroll_TotalForDisplay(1U));
        DFPlayer_Play(DFP_TRACK_ENROLL_FRONT);

    } else if (strcmp(msg, "ENROLL_LEFT") == 0) {
        Mark_ESP32_Alive(); enroll_retry_count = 0; sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(2, Enroll_TotalForDisplay(2U));
        DFPlayer_Play(DFP_TRACK_ENROLL_LEFT);

    } else if (strcmp(msg, "ENROLL_RIGHT") == 0) {
        Mark_ESP32_Alive(); enroll_retry_count = 0; sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(3, Enroll_TotalForDisplay(3U));
        DFPlayer_Play(DFP_TRACK_ENROLL_RIGHT);

    } else if (strcmp(msg, "ENROLL_UP") == 0) {
        Mark_ESP32_Alive(); enroll_retry_count = 0; sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(4, Enroll_TotalForDisplay(4U));
        DFPlayer_Play(DFP_TRACK_ENROLL_UP);

    } else if (strcmp(msg, "ENROLL_DOWN") == 0) {
        Mark_ESP32_Alive(); enroll_retry_count = 0; sys_state = SYS_ENROLLING;
        state_tick = HAL_GetTick();
        SSD1306_ShowEnrollStep(5, Enroll_TotalForDisplay(5U));
        DFPlayer_Play(DFP_TRACK_ENROLL_DOWN);

    } else if (strncmp(msg, "LOCKOUT:", 8) == 0 || strcmp(msg, "LOCKOUT") == 0) {
        Mark_ESP32_Alive();
        if (msg[7] == ':') {
            uint32_t dur = (uint32_t)strtoul(msg + 8, NULL, 10);
            lockout_duration_ms = (dur > 0U) ? dur : LOCKOUT_DISPLAY_MS;
        } else {
            lockout_duration_ms = LOCKOUT_DISPLAY_MS;
        }
        lockout_last_second = 0xFFFFFFFFU;
        sys_state = SYS_LOCKED; state_tick = HAL_GetTick();
        SSD1306_ShowLockout(); DFPlayer_Play(DFP_TRACK_DENIED);

    } else if (strcmp(msg, "LOCKOUT_CLEAR") == 0) {
        Mark_ESP32_Alive();
        if (sys_state == SYS_LOCKED) { sys_state = SYS_IDLE; Show_Ready(); }
    }
}

/* -----------------------------------------------------------------------
 * Application initialisation
 * ----------------------------------------------------------------------- */
static void App_Init(void)
{
    uint32_t now = HAL_GetTick();

    __HAL_GPIO_EXTI_CLEAR_IT(BTN_ENROLL_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_DELETE_Pin);
    __HAL_GPIO_EXTI_CLEAR_IT(BTN_EXIT_Pin);
    btn_enroll_flag = 0; btn_delete_flag = 0; btn_exit_flag = 0;
    btn_exit_tick   = now; btn_enroll_tick = now; btn_delete_tick = now;
    btn_exit_armed   = Button_IsPressed(BTN_EXIT_GPIO_Port,   BTN_EXIT_Pin)   ? 0U : 1U;
    btn_enroll_armed = Button_IsPressed(BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin) ? 0U : 1U;
    btn_delete_armed = Button_IsPressed(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) ? 0U : 1U;
    esp32_ready = 0; delete_hold_active = 0;
    uart1_queue_head = 0; uart1_queue_tail = 0;
    esp32_sync_start_tick = HAL_GetTick(); esp32_status_tick = 0;
    esp32_last_rx_tick = now;
    esp32_offline_tick = 0U;
    esp32_probe_tick = 0U;
    esp32_soft_reset_count = 0U;
    esp32_hard_reset_count = 0U;
    uart_secure_active = 0U;
    uart_secure_tx_seq = 1U;
    uart_secure_rx_seq = 0U;
    uart_secure_rx_synced = 0U;
    uart_secure_hello_tick = 0U;

    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, esp32_dma_rx_buf, ESP32_DMA_RX_SIZE);
    __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);

    UART_RequestSecureHello(1U);
    ESP32_RequestStatus(1);

    sys_state = SYS_CONNECTING; state_tick = HAL_GetTick();

    SSD1306_Init();
    SSD1306_ShowBoot();
    SSD1306_ShowConnecting();

    DFPlayer_Init();
}

/* -----------------------------------------------------------------------
 * Main application loop body
 * ----------------------------------------------------------------------- */
static void App_Loop(void)
{
    IWDG->KR = 0xAAAAU;
    uint32_t now = HAL_GetTick();
    char uart1_msg[UART1_BUF_SIZE];
    char decoded_msg[UART1_BUF_SIZE];

    if (relay_open_active && ((now - relay_open_tick) >= RELAY_OPEN_MS)) {
        Relay_Close();
        if (sys_state == SYS_UNLOCKING) Restore_LinkAwareIdle();
        now = HAL_GetTick();
    }

    Button_PollPress(&btn_exit_armed, &btn_exit_flag, &btn_exit_tick,
                     BTN_EXIT_GPIO_Port, BTN_EXIT_Pin, now);
    Button_PollPress(&btn_enroll_armed, &btn_enroll_flag, &btn_enroll_tick,
                     BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin, now);
    Button_PollPress(&btn_delete_armed, &btn_delete_flag, &btn_delete_tick,
                     BTN_DELETE_GPIO_Port, BTN_DELETE_Pin, now);
    Button_PollRelease(&btn_exit_armed,   &btn_exit_tick,   BTN_EXIT_GPIO_Port,   BTN_EXIT_Pin,   now);
    Button_PollRelease(&btn_enroll_armed, &btn_enroll_tick, BTN_ENROLL_GPIO_Port, BTN_ENROLL_Pin, now);
    Button_PollRelease(&btn_delete_armed, &btn_delete_tick, BTN_DELETE_GPIO_Port, BTN_DELETE_Pin, now);

    if (btn_exit_flag) {
        btn_exit_flag = 0; delete_hold_active = 0;
        if (sys_state == SYS_ENROLLING && esp32_ready)
            (void)UART_SendCommand("CANCEL", 0U);
        if (sys_state == SYS_UNLOCKING) {
            state_tick = HAL_GetTick(); relay_open_tick = state_tick; relay_open_active = 1;
        } else {
            Enter_Unlocked(0, 1);
        }
    }

    while (UART1_DequeueLine(uart1_msg)) {
        if (UART_DecodeLine(uart1_msg, decoded_msg, sizeof(decoded_msg))) {
            Parse_ESP32_Msg(decoded_msg);
            now = HAL_GetTick();
        }
    }

    if (!uart_secure_active) UART_RequestSecureHello(0U);

    if (esp32_ready) {
        uint32_t silent_ms = now - esp32_last_rx_tick;

        if (silent_ms >= ESP32_LINK_WARN_MS) {
            if (esp32_probe_tick == 0U || (now - esp32_probe_tick) >= ESP32_SYNC_RETRY_MS) {
                esp32_probe_tick = now;
                ESP32_RequestStatus(1U);
            }
        }

        if (silent_ms >= ESP32_LINK_LOSS_MS) {
            Mark_ESP32_Offline();
        }
    }

    if (!esp32_ready) ESP32_RequestStatus(0);
    ESP32_TryRecover(now);

    if (delete_hold_active) {
        uint32_t held_ms = now - delete_hold_start;
        uint8_t  sec     = (uint8_t)(held_ms / 1000U);

        if (held_ms > 30000U) {
            delete_hold_active = 0; sys_state = SYS_IDLE; Show_Ready();
        } else if (HAL_GPIO_ReadPin(BTN_DELETE_GPIO_Port, BTN_DELETE_Pin) != GPIO_PIN_RESET) {
            delete_hold_active = 0; sys_state = SYS_IDLE; Show_Ready();
        } else {
            if (sec != delete_last_second) { delete_last_second = sec; SSD1306_ShowHoldDelete(sec); }
            if (held_ms >= DELETE_HOLD_MS) {
                delete_hold_active = 0; SSD1306_ShowDeleting();
                if (UART_SendCommand("DEL_ALL", 0U) == HAL_OK) {
                    state_tick = now;
                } else {
                    Mark_ESP32_Offline();
                }
            }
        }
    }

    if (btn_enroll_flag) {
        btn_enroll_flag = 0;
        if (sys_state == SYS_IDLE && esp32_ready) {
            enroll_retry_count = 0; sys_state = SYS_ENROLLING; state_tick = HAL_GetTick();
            (void)UART_SendCommand("ENROLL", 0U);
            SSD1306_ShowEnrolling();
        } else if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE || !esp32_ready) {
            ESP32_RequestStatus(1); Show_ESP32_LinkState();
        }
    }

    if (btn_delete_flag) {
        btn_delete_flag = 0;
        if (sys_state == SYS_IDLE && esp32_ready && !delete_hold_active) {
            sys_state = SYS_DELETING; delete_hold_active = 1;
            delete_hold_start = now; delete_last_second = 0xFF; SSD1306_ShowHoldDelete(0);
        } else if (sys_state == SYS_CONNECTING || sys_state == SYS_OFFLINE || !esp32_ready) {
            ESP32_RequestStatus(1); Show_ESP32_LinkState();
        }
    }

    switch (sys_state) {
        case SYS_CONNECTING:
            if ((now - state_tick) >= CONNECT_TIMEOUT_MS) Mark_ESP32_Offline();
            break;
        case SYS_UNLOCKING:
            if (!relay_open_active) Restore_LinkAwareIdle();
            break;
        case SYS_DENIED:
            if ((now - state_tick) >= DENIED_DISPLAY_MS) Restore_LinkAwareIdle();
            break;
        case SYS_ENROLLING:
            if ((now - state_tick) >= ENROLL_TIMEOUT_MS) {
                if (esp32_ready && (enroll_retry_count < ENROLL_RETRY_MAX)) {
                    enroll_retry_count++; state_tick = now;
                    (void)UART_SendCommand("CANCEL", 0U);
                    (void)UART_SendCommand("ENROLL", 0U);
                    SSD1306_ShowEnrolling(); break;
                }
                btn_enroll_flag = 0;
                (void)UART_SendCommand("CANCEL", 0U);
                enroll_retry_count = 0;
                sys_state = SYS_RESULT; state_tick = HAL_GetTick();
                SSD1306_ShowDenied(); DFPlayer_Play(DFP_TRACK_DENIED);
                ESP32_RequestStatus(1);
            }
            break;
        case SYS_DELETING:
            if (!delete_hold_active && (now - state_tick) >= 10000U) {
                Mark_ESP32_Offline(); ESP32_RequestStatus(1);
            }
            break;
        case SYS_RESULT:
            if ((now - state_tick) >= RESULT_DISPLAY_MS) Restore_LinkAwareIdle();
            break;
        case SYS_LOCKED: {
            uint32_t elapsed_ms = now - state_tick;
            if (elapsed_ms >= lockout_duration_ms) {
                lockout_last_second = 0xFFFFFFFFU; Restore_LinkAwareIdle();
            } else {
                uint32_t remaining_s = (lockout_duration_ms - elapsed_ms + 999U) / 1000U;
                if (remaining_s != lockout_last_second) {
                    lockout_last_second = remaining_s; SSD1306_ShowLockedCountdown(remaining_s);
                }
            }
            break;
        }
        default: break;
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
  MX_USART1_UART_Init();
  MX_I2C1_Init();
  MX_USART2_UART_Init();
  MX_USART6_UART_Init();
  /* USER CODE BEGIN 2 */
  App_Init();

  /* ── Independent Watchdog (IWDG) ──────────────────────────────────────────
   * STM32F411 LSI ≈ 32 kHz, prescaler = 64, reload = 2499 → timeout ≈ 5.0 s
   * Started AFTER App_Init so DFPlayer HAL_Delay calls don't trigger a reset.
   * ─────────────────────────────────────────────────────────────────────── */
  IWDG->KR  = 0xCCCCU;           /* Enable IWDG                            */
  IWDG->KR  = 0x5555U;           /* Unlock PR / RLR registers              */
  IWDG->PR  = IWDG_PR_PR_2;      /* Prescaler = 64                         */
  IWDG->RLR = 2499U;             /* Reload → 2499 × (64/32000) ≈ 5.0 s    */
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
    __disable_irq();
    if ((uart1_queue_head == uart1_queue_tail) && !btn_exit_flag &&
        !btn_enroll_flag  && !btn_delete_flag) {
        __WFI();
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

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 100;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
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
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 9600;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream2_IRQn);

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
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(ESP32_RST_GPIO_Port, ESP32_RST_Pin, GPIO_PIN_SET);

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

  /*Configure GPIO pin : ESP32_RST_Pin */
  GPIO_InitStruct.Pin = ESP32_RST_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(ESP32_RST_GPIO_Port, &GPIO_InitStruct);

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
