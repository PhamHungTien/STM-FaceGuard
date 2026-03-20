/*
 * app_main.cpp  —  FaceGuard ESP32-S3-CAM (AI-Thinker)
 * Face Recognition Door Lock — ESP-IDF 5.x
 *
 * ── Wiring ────────────────────────────────────────────────────────────────────
 *   ESP32 GPIO47 TX  →  STM32 PA10 (USART1 RX)
 *   ESP32 GPIO21 RX  ←  STM32 PA9  (USART1 TX)
 *   GND              ↔  GND
 *
 * ── Protocol STM32 → ESP32 ───────────────────────────────────────────────────
 *   "ENROLL\n"     start enrollment
 *   "DEL_ALL\n"    delete all faces
 *   "CANCEL\n"     cancel enrollment
 *
 * ── Protocol ESP32 → STM32 ───────────────────────────────────────────────────
 *   "READY\n"           boot complete
 *   "OPEN:<id>\n"       face recognised
 *   "DENIED\n"          face not recognised
 *   "ENROLLED:<id>\n"   new face saved
 *   "DELETED\n"         all faces cleared
 *   "ENROLL_FRONT\n"    step 1/5
 *   "ENROLL_LEFT\n"     step 2/5
 *   "ENROLL_RIGHT\n"    step 3/5
 *   "ENROLL_UP\n"       step 4/5
 *   "ENROLL_DOWN\n"     step 5/5
 */

#include <cstring>
#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_camera.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "nvs_flash.h"

// Face AI — provided by espressif/esp-dl component
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s16.hpp"

static const char *TAG = "FaceGuard";

// ═══════════════════════════════════════════════════════════
//  Camera pins — AI-Thinker ESP32-S3-CAM
// ═══════════════════════════════════════════════════════════
#define CAM_PWDN   -1
#define CAM_RESET  -1
#define CAM_XCLK   15
#define CAM_SDA     4
#define CAM_SCL     5
#define CAM_D9     16
#define CAM_D8     17
#define CAM_D7     18
#define CAM_D6     12
#define CAM_D5     10
#define CAM_D4      8
#define CAM_D3      9
#define CAM_D2     11
#define CAM_VSYNC   6
#define CAM_HREF    7
#define CAM_PCLK   13

// ═══════════════════════════════════════════════════════════
//  UART to STM32
// ═══════════════════════════════════════════════════════════
#define UART_STM32_PORT     UART_NUM_1
#define UART_STM32_TX_PIN   47
#define UART_STM32_RX_PIN   21
#define UART_STM32_BAUD     115200
#define UART_BUF_SIZE       256

// ═══════════════════════════════════════════════════════════
//  Face recognition config
// ═══════════════════════════════════════════════════════════
#define FACE_ID_MAX          7
#define ENROLL_SAMPLES       5    // one per pose
#define RECOG_THRESHOLD      0.8f

#define NUM_POSES            5
static const char *POSE_MSG[NUM_POSES] = {
    "ENROLL_FRONT", "ENROLL_LEFT", "ENROLL_RIGHT", "ENROLL_UP", "ENROLL_DOWN"
};

// ═══════════════════════════════════════════════════════════
//  State machine
// ═══════════════════════════════════════════════════════════
typedef enum { STATE_IDLE = 0, STATE_ENROLLING, STATE_DELETING } SysState_t;

static volatile SysState_t g_state     = STATE_IDLE;
static volatile int        g_pose_step = 0;
static SemaphoreHandle_t   g_state_mutex;

// ═══════════════════════════════════════════════════════════
//  Face AI objects (constructed in main task)
// ═══════════════════════════════════════════════════════════
static HumanFaceDetectMSR01    *s_detector   = nullptr;
static FaceRecognition112V1S16 *s_recognizer = nullptr;

// ═══════════════════════════════════════════════════════════
//  UART helpers
// ═══════════════════════════════════════════════════════════
static void uart_send(const char *msg)
{
    // Append \n if not present
    char buf[64];
    snprintf(buf, sizeof(buf), "%s\n", msg);
    uart_write_bytes(UART_STM32_PORT, buf, strlen(buf));
    ESP_LOGI(TAG, "→STM32: %s", msg);
}

static void uart_init(void)
{
    uart_config_t cfg = {
        .baud_rate           = UART_STM32_BAUD,
        .data_bits           = UART_DATA_8_BITS,
        .parity              = UART_PARITY_DISABLE,
        .stop_bits           = UART_STOP_BITS_1,
        .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk          = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_STM32_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_STM32_PORT, &cfg);
    uart_set_pin(UART_STM32_PORT,
                 UART_STM32_TX_PIN, UART_STM32_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// ═══════════════════════════════════════════════════════════
//  UART RX task — listens for commands from STM32
// ═══════════════════════════════════════════════════════════
static void uart_rx_task(void *arg)
{
    uint8_t buf[64];
    int  idx = 0;

    while (1) {
        uint8_t byte;
        int len = uart_read_bytes(UART_STM32_PORT, &byte, 1, pdMS_TO_TICKS(20));
        if (len <= 0) continue;

        if (byte == '\n' || byte == '\r') {
            if (idx == 0) continue;
            buf[idx] = '\0';
            idx = 0;

            const char *cmd = (const char *)buf;
            ESP_LOGI(TAG, "←STM32: %s", cmd);

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            if (strcmp(cmd, "ENROLL") == 0 && g_state == STATE_IDLE) {
                g_state     = STATE_ENROLLING;
                g_pose_step = 0;
                xSemaphoreGive(g_state_mutex);
                uart_send(POSE_MSG[0]);   // ENROLL_FRONT

            } else if (strcmp(cmd, "DEL_ALL") == 0) {
                g_state = STATE_DELETING;
                xSemaphoreGive(g_state_mutex);

            } else if (strcmp(cmd, "CANCEL") == 0) {
                g_state     = STATE_IDLE;
                g_pose_step = 0;
                xSemaphoreGive(g_state_mutex);

            } else {
                xSemaphoreGive(g_state_mutex);
            }
        } else if (idx < 63) {
            buf[idx++] = byte;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Camera initialisation
// ═══════════════════════════════════════════════════════════
static esp_err_t camera_init(void)
{
    camera_config_t cfg = {};
    cfg.ledc_channel  = LEDC_CHANNEL_0;
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = CAM_D2;
    cfg.pin_d1        = CAM_D3;
    cfg.pin_d2        = CAM_D4;
    cfg.pin_d3        = CAM_D5;
    cfg.pin_d4        = CAM_D6;
    cfg.pin_d5        = CAM_D7;
    cfg.pin_d6        = CAM_D8;
    cfg.pin_d7        = CAM_D9;
    cfg.pin_xclk      = CAM_XCLK;
    cfg.pin_pclk      = CAM_PCLK;
    cfg.pin_vsync     = CAM_VSYNC;
    cfg.pin_href      = CAM_HREF;
    cfg.pin_sccb_sda  = CAM_SDA;
    cfg.pin_sccb_scl  = CAM_SCL;
    cfg.pin_pwdn      = CAM_PWDN;
    cfg.pin_reset     = CAM_RESET;
    cfg.xclk_freq_hz  = 20000000;
    cfg.pixel_format  = PIXFORMAT_RGB565;
    cfg.frame_size    = FRAMESIZE_240X240;
    cfg.jpeg_quality  = 12;
    cfg.fb_count      = 2;
    cfg.fb_location   = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    esp_err_t ret = esp_camera_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", ret);
        return ret;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    ESP_LOGI(TAG, "Camera OK");
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════════
//  STATE_IDLE — run face recognition
// ═══════════════════════════════════════════════════════════
static void do_recognition(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    std::list<dl::detect::result_t> &detections = s_detector->infer(fb);

    if (detections.empty()) {
        esp_camera_fb_return(fb);
        return;
    }

    face_info_t info = s_recognizer->recognize(fb, detections.front().keypoint);
    esp_camera_fb_return(fb);

    if (info.id >= 0 && info.similarity >= RECOG_THRESHOLD) {
        char msg[24];
        snprintf(msg, sizeof(msg), "OPEN:%d", info.id);
        uart_send(msg);
        vTaskDelay(pdMS_TO_TICKS(4000));
    } else {
        uart_send("DENIED");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// ═══════════════════════════════════════════════════════════
//  STATE_ENROLLING — 5-pose guided enrollment
// ═══════════════════════════════════════════════════════════
static void do_enroll(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(50)); return; }

    std::list<dl::detect::result_t> &detections = s_detector->infer(fb);

    if (detections.empty()) {
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(150));
        return;
    }

    int ret = s_recognizer->enroll_id(fb, detections.front().keypoint, "", true);
    esp_camera_fb_return(fb);

    if (ret > 0) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        g_pose_step++;
        int step = g_pose_step;
        xSemaphoreGive(g_state_mutex);

        ESP_LOGI(TAG, "Enrolled sample %d/%d", step, ENROLL_SAMPLES);

        if (step < NUM_POSES) {
            uart_send(POSE_MSG[step]);
            vTaskDelay(pdMS_TO_TICKS(800));
        }

        if (ret == ENROLL_SAMPLES) {
            int id = s_recognizer->get_enrolled_id_num() - 1;
            char msg[24];
            snprintf(msg, sizeof(msg), "ENROLLED:%d", id);
            uart_send(msg);
            ESP_LOGI(TAG, "Face %d enrolled (total: %d)",
                     id, s_recognizer->get_enrolled_id_num());

            xSemaphoreTake(g_state_mutex, portMAX_DELAY);
            g_state     = STATE_IDLE;
            g_pose_step = 0;
            xSemaphoreGive(g_state_mutex);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ═══════════════════════════════════════════════════════════
//  STATE_DELETING — wipe all faces
// ═══════════════════════════════════════════════════════════
static void do_delete(void)
{
    s_recognizer->clear_id();
    uart_send("DELETED");
    ESP_LOGI(TAG, "All faces deleted");

    xSemaphoreTake(g_state_mutex, portMAX_DELAY);
    g_state = STATE_IDLE;
    xSemaphoreGive(g_state_mutex);
}

// ═══════════════════════════════════════════════════════════
//  Main face recognition task
// ═══════════════════════════════════════════════════════════
static void face_task(void *arg)
{
    while (1) {
        xSemaphoreTake(g_state_mutex, portMAX_DELAY);
        SysState_t state = g_state;
        xSemaphoreGive(g_state_mutex);

        switch (state) {
            case STATE_IDLE:      do_recognition(); break;
            case STATE_ENROLLING: do_enroll();      break;
            case STATE_DELETING:  do_delete();      break;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  app_main
// ═══════════════════════════════════════════════════════════
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== FaceGuard ESP32-S3 ===");

    // NVS (needed for some internal components)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // Mutex for state machine
    g_state_mutex = xSemaphoreCreateMutex();

    // UART to STM32
    uart_init();

    // Camera
    if (camera_init() != ESP_OK) {
        ESP_LOGE(TAG, "Camera failed — restarting");
        esp_restart();
    }

    // Face AI — create on heap to avoid stack overflow
    s_detector   = new HumanFaceDetectMSR01(0.3f, 0.3f, 10, 0.3f);
    s_recognizer = new FaceRecognition112V1S16(ENROLL_SAMPLES);

    // Load face database from "fr" partition
    s_recognizer->set_partition(ESP_PARTITION_TYPE_DATA,
                                ESP_PARTITION_SUBTYPE_ANY, "fr");
    s_recognizer->set_ids_from_flash();
    ESP_LOGI(TAG, "Faces in DB: %d", s_recognizer->get_enrolled_id_num());

    // Start tasks
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(face_task,    "face",    8192, NULL, 5, NULL, 1);

    // Signal STM32 we're ready
    uart_send("READY");

    // Main task can delete itself
    vTaskDelete(NULL);
}
