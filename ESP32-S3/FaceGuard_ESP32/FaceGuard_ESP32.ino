/*
 * FaceGuard_ESP32.ino
 * ESP32-S3-CAM (AI-Thinker) — Face Recognition Door Lock Controller
 *
 * ── Arduino IDE Settings ─────────────────────────────────────────────────────
 *   Board:              ESP32S3 Dev Module
 *   Flash Size:         16MB (128Mb)
 *   PSRAM:              OPI PSRAM
 *   Partition Scheme:   Custom  ← uses partitions.csv in this folder
 *   USB CDC on Boot:    Disabled
 *   Upload Mode:        UART0 / Hardware CDC
 *   Upload Speed:       921600
 *
 * ── Wiring: ESP32-S3-CAM ↔ STM32F303RE (Nucleo) ─────────────────────────────
 *   ESP32 GPIO47 (TX)  →  STM32 PA10 (USART1 RX)
 *   ESP32 GPIO21 (RX)  ←  STM32 PA9  (USART1 TX)
 *   GND                ↔  GND
 *
 * ── UART Protocol ────────────────────────────────────────────────────────────
 *   STM32 → ESP32:
 *     "ENROLL\n"    start enrollment session
 *     "DEL_ALL\n"   delete all enrolled faces
 *     "CANCEL\n"    cancel current operation
 *
 *   ESP32 → STM32:
 *     "READY\n"           boot complete
 *     "OPEN:<id>\n"       face recognised (id = 0-based index)
 *     "DENIED\n"          face not recognised
 *     "ENROLLED:<id>\n"   new face saved
 *     "DELETED\n"         all faces cleared
 *     "ENROLL_FRONT\n"    step 1 — look straight
 *     "ENROLL_LEFT\n"     step 2 — turn left
 *     "ENROLL_RIGHT\n"    step 3 — turn right
 *     "ENROLL_UP\n"       step 4 — tilt up
 *     "ENROLL_DOWN\n"     step 5 — tilt down
 */

#include "esp_camera.h"
#include "HardwareSerial.h"

// Face AI — requires arduino-esp32 ≥ 3.0 (ESP-DL bundled)
#include "human_face_detect_msr01.hpp"
#include "face_recognition_112_v1_s16.hpp"

// ═══════════════════════════════════════════════════════════
//  Camera pin definitions — AI-Thinker ESP32-S3-CAM
// ═══════════════════════════════════════════════════════════
#define PWDN_GPIO_NUM    -1
#define RESET_GPIO_NUM   -1
#define XCLK_GPIO_NUM    15
#define SIOD_GPIO_NUM     4
#define SIOC_GPIO_NUM     5
#define Y9_GPIO_NUM      16
#define Y8_GPIO_NUM      17
#define Y7_GPIO_NUM      18
#define Y6_GPIO_NUM      12
#define Y5_GPIO_NUM      10
#define Y4_GPIO_NUM       8
#define Y3_GPIO_NUM       9
#define Y2_GPIO_NUM      11
#define VSYNC_GPIO_NUM    6
#define HREF_GPIO_NUM     7
#define PCLK_GPIO_NUM    13

// ═══════════════════════════════════════════════════════════
//  UART to STM32
// ═══════════════════════════════════════════════════════════
#define PIN_TX_STM32     47       // → STM32 PA10 (USART1 RX)
#define PIN_RX_STM32     21       // ← STM32 PA9  (USART1 TX)
#define BAUD_STM32       115200

// ═══════════════════════════════════════════════════════════
//  Face recognition config
// ═══════════════════════════════════════════════════════════
#define FACE_ID_MAX          7    // Max enrolled faces
#define ENROLL_SAMPLES       5    // One sample per pose (5 poses total)
#define RECOG_THRESHOLD      0.8f // Cosine similarity threshold

// ═══════════════════════════════════════════════════════════
//  State machine
// ═══════════════════════════════════════════════════════════
typedef enum { STATE_IDLE = 0, STATE_ENROLLING, STATE_DELETING } SysState_t;

#define NUM_POSES  5
static const char *POSE_MSG[NUM_POSES] = {
    "ENROLL_FRONT", "ENROLL_LEFT", "ENROLL_RIGHT", "ENROLL_UP", "ENROLL_DOWN"
};

// ═══════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════
static HardwareSerial SerialSTM(1);   // UART1

static SysState_t g_state      = STATE_IDLE;
static int        g_pose_step  = 0;   // Current enrollment pose (0-4)

// UART receive buffer
static char g_rx_buf[64];
static int  g_rx_idx = 0;

// Face AI objects (ENROLL_SAMPLES = 5 → one sample per pose)
static HumanFaceDetectMSR01    s_detector(0.3f, 0.3f, 10, 0.3f);
static FaceRecognition112V1S16 s_recognizer(ENROLL_SAMPLES);

// ═══════════════════════════════════════════════════════════
//  UART helpers
// ═══════════════════════════════════════════════════════════
static void toSTM32(const char *msg)
{
    SerialSTM.println(msg);
    Serial.printf("[→STM32] %s\n", msg);
}

static void handleCmd(const char *cmd)
{
    Serial.printf("[←STM32] %s\n", cmd);

    if (strcmp(cmd, "ENROLL") == 0 && g_state == STATE_IDLE) {
        if (s_recognizer.get_enrolled_id_num() >= FACE_ID_MAX) {
            toSTM32("DENIED");   // DB full — reuse as error signal
            return;
        }
        g_state     = STATE_ENROLLING;
        g_pose_step = 0;
        toSTM32(POSE_MSG[0]);   // "ENROLL_FRONT"

    } else if (strcmp(cmd, "DEL_ALL") == 0) {
        g_state = STATE_DELETING;

    } else if (strcmp(cmd, "CANCEL") == 0) {
        g_state     = STATE_IDLE;
        g_pose_step = 0;
        Serial.println("Enrollment cancelled by STM32");
    }
}

static void pollUART()
{
    while (SerialSTM.available()) {
        char c = (char)SerialSTM.read();
        if (c == '\n' || c == '\r') {
            if (g_rx_idx > 0) {
                g_rx_buf[g_rx_idx] = '\0';
                handleCmd(g_rx_buf);
                g_rx_idx = 0;
            }
        } else if (g_rx_idx < 63) {
            g_rx_buf[g_rx_idx++] = c;
        }
    }
}

// ═══════════════════════════════════════════════════════════
//  Camera initialisation
// ═══════════════════════════════════════════════════════════
static bool initCamera()
{
    camera_config_t cfg;
    cfg.ledc_channel  = LEDC_CHANNEL_0;
    cfg.ledc_timer    = LEDC_TIMER_0;
    cfg.pin_d0        = Y2_GPIO_NUM;
    cfg.pin_d1        = Y3_GPIO_NUM;
    cfg.pin_d2        = Y4_GPIO_NUM;
    cfg.pin_d3        = Y5_GPIO_NUM;
    cfg.pin_d4        = Y6_GPIO_NUM;
    cfg.pin_d5        = Y7_GPIO_NUM;
    cfg.pin_d6        = Y8_GPIO_NUM;
    cfg.pin_d7        = Y9_GPIO_NUM;
    cfg.pin_xclk      = XCLK_GPIO_NUM;
    cfg.pin_pclk      = PCLK_GPIO_NUM;
    cfg.pin_vsync     = VSYNC_GPIO_NUM;
    cfg.pin_href      = HREF_GPIO_NUM;
    cfg.pin_sccb_sda  = SIOD_GPIO_NUM;
    cfg.pin_sccb_scl  = SIOC_GPIO_NUM;
    cfg.pin_pwdn      = PWDN_GPIO_NUM;
    cfg.pin_reset     = RESET_GPIO_NUM;
    cfg.xclk_freq_hz  = 20000000;
    cfg.pixel_format  = PIXFORMAT_RGB565;    // Required by face AI
    cfg.frame_size    = FRAMESIZE_240X240;   // Required by face AI
    cfg.jpeg_quality  = 12;
    cfg.fb_count      = 2;
    cfg.fb_location   = CAMERA_FB_IN_PSRAM;  // Needs OPI PSRAM enabled
    cfg.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    if (esp_camera_init(&cfg) != ESP_OK) {
        Serial.println("Camera init FAILED");
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_hmirror(s, 0);
    s->set_vflip(s, 0);
    Serial.println("Camera OK");
    return true;
}

// ═══════════════════════════════════════════════════════════
//  STATE_IDLE — continuous face recognition
// ═══════════════════════════════════════════════════════════
static void runIdle()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    std::list<dl::detect::result_t> &detections = s_detector.infer(fb);

    if (detections.empty()) {
        esp_camera_fb_return(fb);
        return;
    }

    face_info_t info = s_recognizer.recognize(fb, detections.front().keypoint);
    esp_camera_fb_return(fb);

    if (info.id >= 0 && info.similarity >= RECOG_THRESHOLD) {
        char msg[24];
        snprintf(msg, sizeof(msg), "OPEN:%d", info.id);
        toSTM32(msg);
        delay(4000);   // Cooldown: prevent rapid re-trigger
    } else {
        toSTM32("DENIED");
        delay(3000);
    }
}

// ═══════════════════════════════════════════════════════════
//  STATE_ENROLLING — 5-pose guided enrollment
//  One sample captured per pose; library averages all 5.
// ═══════════════════════════════════════════════════════════
static void runEnroll()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    std::list<dl::detect::result_t> &detections = s_detector.infer(fb);

    if (detections.empty()) {
        esp_camera_fb_return(fb);
        delay(150);
        return;
    }

    // enroll_id returns the number of samples collected so far
    int ret = s_recognizer.enroll_id(fb, detections.front().keypoint, "", true);
    esp_camera_fb_return(fb);

    Serial.printf("Enroll pose %d/%d — sample #%d\n",
                  g_pose_step + 1, NUM_POSES, ret);

    if (ret > 0) {
        // This pose captured — advance to next
        g_pose_step++;

        if (g_pose_step < NUM_POSES) {
            toSTM32(POSE_MSG[g_pose_step]);
            delay(800);   // Give user time to move
        }

        if (ret == ENROLL_SAMPLES) {
            // All 5 samples done — face saved
            int id = s_recognizer.get_enrolled_id_num() - 1;
            char msg[24];
            snprintf(msg, sizeof(msg), "ENROLLED:%d", id);
            toSTM32(msg);
            Serial.printf("Enrolled face ID %d  (total: %d)\n",
                          id, s_recognizer.get_enrolled_id_num());
            g_state     = STATE_IDLE;
            g_pose_step = 0;
        }
    }
    // If ret == 0, face was not accepted — stay on same pose, try again
}

// ═══════════════════════════════════════════════════════════
//  STATE_DELETING — wipe all faces from DB
// ═══════════════════════════════════════════════════════════
static void runDelete()
{
    s_recognizer.clear_id();
    toSTM32("DELETED");
    Serial.println("All faces deleted");
    g_state = STATE_IDLE;
}

// ═══════════════════════════════════════════════════════════
//  Arduino entry points
// ═══════════════════════════════════════════════════════════
void setup()
{
    Serial.begin(115200);
    Serial.println("\n=== FaceGuard ESP32-S3 ===");

    // UART1 to STM32
    SerialSTM.begin(BAUD_STM32, SERIAL_8N1, PIN_RX_STM32, PIN_TX_STM32);

    // Camera
    if (!initCamera()) {
        Serial.println("Camera failed — restart in 5 s");
        delay(5000);
        ESP.restart();
    }

    // Load face database from "fr" partition (see partitions.csv)
    s_recognizer.set_partition(ESP_PARTITION_TYPE_DATA,
                               ESP_PARTITION_SUBTYPE_ANY, "fr");
    s_recognizer.set_ids_from_flash();
    Serial.printf("Faces in DB: %d\n", s_recognizer.get_enrolled_id_num());

    toSTM32("READY");
}

void loop()
{
    pollUART();

    switch (g_state) {
        case STATE_IDLE:      runIdle();   break;
        case STATE_ENROLLING: runEnroll(); break;
        case STATE_DELETING:  runDelete(); break;
    }
}
