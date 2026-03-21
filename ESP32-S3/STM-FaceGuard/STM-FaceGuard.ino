/*
 * STM-FaceGuard — ESP32-S3 Face Recognition Node
 * ─────────────────────────────────────────────────────────────────────────────
 * Board   : ESP32-S3 N16R8 (Freenove ESP32-S3 WROOM hoặc tương đương)
 * Arduino : ESP32 Arduino core >= 2.0.6
 *   - Board: "ESP32S3 Dev Module"
 *   - USB CDC On Boot: Enabled
 *   - PSRAM: OPI PSRAM
 *   - Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
 *
 * Wiring (ESP32-S3 <-> STM32F303RE Nucleo):
 *   GPIO1  (ESP TX) --> PA10 (STM32 USART1_RX)
 *   GPIO2  (ESP RX) <-- PA9  (STM32 USART1_TX)
 *   GND             --- GND
 *
 * ── Giao thức STM32 → ESP32 ──────────────────────────────────────────────────
 *   "ENROLL\n"   – bắt đầu đăng ký khuôn mặt mới
 *   "DEL_ALL\n"  – xoá toàn bộ khuôn mặt khỏi flash
 *   "CANCEL\n"   – huỷ đăng ký đang diễn ra
 *
 * ── Giao thức ESP32 → STM32 ──────────────────────────────────────────────────
 *   "READY\n"          – khởi động xong
 *   "OPEN:<id>\n"      – nhận ra khuôn mặt id
 *   "DENIED\n"         – không nhận ra
 *   "ENROLLED:<id>\n"  – đăng ký thành công, ID mới = id
 *   "DELETED\n"        – đã xoá toàn bộ
 *   "ENROLL_FRONT\n"   – bước 1/5: nhìn thẳng
 *   "ENROLL_LEFT\n"    – bước 2/5: quay trái
 *   "ENROLL_RIGHT\n"   – bước 3/5: quay phải
 *   "ENROLL_UP\n"      – bước 4/5: ngẩng đầu
 *   "ENROLL_DOWN\n"    – bước 5/5: cúi đầu
 * ─────────────────────────────────────────────────────────────────────────────
 */

#include "esp_camera.h"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_tool.hpp"
#include "face_recognition_112_v1_s8.hpp"

// ── Camera pins — Freenove ESP32-S3 WROOM (OV2640) ───────────────────────────
// Thay đổi nếu dùng module camera khác
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   15
#define SIOD_GPIO_NUM    4
#define SIOC_GPIO_NUM    5
#define Y9_GPIO_NUM     16
#define Y8_GPIO_NUM     17
#define Y7_GPIO_NUM     18
#define Y6_GPIO_NUM     12
#define Y5_GPIO_NUM     10
#define Y4_GPIO_NUM      8
#define Y3_GPIO_NUM      9
#define Y2_GPIO_NUM     11
#define VSYNC_GPIO_NUM   6
#define HREF_GPIO_NUM    7
#define PCLK_GPIO_NUM   13

// ── UART tới STM32 ────────────────────────────────────────────────────────────
#define STM32_BAUD     115200
#define STM32_TX_PIN       1    // GPIO1 → STM32 PA10 (USART1_RX)
#define STM32_RX_PIN       2    // GPIO2 ← STM32 PA9  (USART1_TX)

// ── Tham số tuning ────────────────────────────────────────────────────────────
#define RECOGNITION_COOLDOWN_MS   3000   // ms giữa 2 lần nhận diện (tránh spam)
#define ENROLL_FACE_TIMEOUT_MS   10000   // ms chờ phát hiện khuôn mặt mỗi bước
#define ENROLL_TOTAL_STEPS           5

// ── Chuỗi prompt gửi STM32 khi đăng ký ───────────────────────────────────────
static const char * const ENROLL_STEPS[ENROLL_TOTAL_STEPS] = {
    "ENROLL_FRONT",   // bước 1: nhìn thẳng
    "ENROLL_LEFT",    // bước 2: quay trái
    "ENROLL_RIGHT",   // bước 3: quay phải
    "ENROLL_UP",      // bước 4: ngẩng đầu
    "ENROLL_DOWN"     // bước 5: cúi đầu
};

// ── State machine ─────────────────────────────────────────────────────────────
enum AppState { STATE_IDLE, STATE_ENROLLING };
static AppState appState    = STATE_IDLE;
static int      enrollStep  = 0;     // bước hiện tại 0..4
static uint32_t stepStartMs = 0;     // millis() lúc bắt đầu bước

// ── Face AI objects ───────────────────────────────────────────────────────────
// Detector params: score_threshold, nms_threshold, top_k, min_face_size
static HumanFaceDetectMSR01   detector(0.3F, 0.3F, 10, 0.3F);
static FaceRecognition112V1S8 recognizer;   // tự nạp face DB từ NVS/flash

// ── Misc ──────────────────────────────────────────────────────────────────────
static uint32_t lastRecogMs = 0;
static String   rxBuf;              // bộ đệm dòng UART từ STM32

// ─────────────────────────────────────────────────────────────────────────────
// Khởi tạo camera
// ─────────────────────────────────────────────────────────────────────────────
static bool camera_init()
{
    camera_config_t cfg = {};
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
    cfg.pixel_format  = PIXFORMAT_RGB888;    // 3-byte/pixel, sẵn sàng cho AI
    cfg.frame_size    = FRAMESIZE_240X240;   // cân bằng tốt cho face AI
    cfg.fb_count      = psramFound() ? 2 : 1;
    cfg.fb_location   = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    cfg.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    if (esp_camera_init(&cfg) != ESP_OK) return false;

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 1);      // gương ngang — nhìn tự nhiên như selfie
        s->set_awb_gain(s, 1);
        s->set_exposure_ctrl(s, 1);
        s->set_aec2(s, 1);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Xử lý lệnh nhận từ STM32
// ─────────────────────────────────────────────────────────────────────────────
static void process_cmd(const String &cmd)
{
    Serial.printf("[CMD] «%s»\n", cmd.c_str());

    if (cmd == "ENROLL") {
        if (appState == STATE_IDLE) {
            appState    = STATE_ENROLLING;
            enrollStep  = 0;
            stepStartMs = millis();
            // Gửi hướng dẫn bước đầu tiên cho STM32
            Serial1.printf("%s\n", ENROLL_STEPS[0]);
            Serial.printf("[ENROLL] Step 1/%d → %s\n", ENROLL_TOTAL_STEPS, ENROLL_STEPS[0]);
        }
    }
    else if (cmd == "DEL_ALL") {
        recognizer.clear_id();
        appState = STATE_IDLE;
        Serial1.println("DELETED");
        Serial.println("[SYS] Face DB cleared");
    }
    else if (cmd == "CANCEL") {
        appState = STATE_IDLE;
        Serial.println("[ENROLL] Cancelled by STM32");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Xử lý một frame camera: nhận diện hoặc đăng ký
// ─────────────────────────────────────────────────────────────────────────────
static void process_frame()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    // Phát hiện khuôn mặt (RGB888)
    std::list<dl::detect::result_t> faces =
        detector.infer((uint8_t *)fb->buf, {(int)fb->height, (int)fb->width, 3});

    if (faces.empty()) {
        // Không thấy khuôn mặt
        if (appState == STATE_ENROLLING) {
            if ((millis() - stepStartMs) >= ENROLL_FACE_TIMEOUT_MS) {
                // Hết giờ chờ, nhắc lại bước hiện tại
                stepStartMs = millis();
                Serial1.printf("%s\n", ENROLL_STEPS[enrollStep]);
                Serial.printf("[ENROLL] Timeout, re-prompt step %d\n", enrollStep + 1);
            }
        }
        esp_camera_fb_return(fb);
        return;
    }

    dl::detect::result_t &face = faces.front();

    // Bọc frame buffer thành Tensor (không copy, trỏ thẳng vào fb->buf)
    Tensor<uint8_t> img;
    img.set_element((uint8_t *)fb->buf)
       .set_shape({(int)fb->height, (int)fb->width, 3})
       .set_auto_free(false);

    // ── Chế độ ĐĂNG KÝ ───────────────────────────────────────────────────────
    if (appState == STATE_ENROLLING)
    {
        // is_append=false ở bước đầu: tạo entry mới trong DB
        // is_append=true  ở bước tiếp: gộp thêm mẫu vào entry hiện tại
        bool is_append = (enrollStep > 0);
        int  result    = recognizer.enroll_id(img, face.keypoint, "", is_append);

        bool is_last = (enrollStep == ENROLL_TOTAL_STEPS - 1);

        if (is_last) {
            // Hoàn tất đăng ký — tính ID mới
            int new_id = (result >= 0) ? result
                                       : (recognizer.get_enrolled_id_num() - 1);
            Serial1.printf("ENROLLED:%d\n", new_id);
            Serial.printf("[ENROLL] Done! ID=%d  total=%d faces\n",
                          new_id, recognizer.get_enrolled_id_num());
            appState = STATE_IDLE;
        }
        else {
            // Sang bước tiếp theo
            enrollStep++;
            stepStartMs = millis();
            Serial1.printf("%s\n", ENROLL_STEPS[enrollStep]);
            Serial.printf("[ENROLL] Step %d/%d → %s\n",
                          enrollStep + 1, ENROLL_TOTAL_STEPS, ENROLL_STEPS[enrollStep]);
        }
    }
    // ── Chế độ NHẬN DIỆN (IDLE) ───────────────────────────────────────────────
    else
    {
        uint32_t now = millis();
        if ((now - lastRecogMs) < RECOGNITION_COOLDOWN_MS) {
            // Còn trong cooldown, bỏ qua frame này
            esp_camera_fb_return(fb);
            return;
        }
        lastRecogMs = now;

        face_info_t res = recognizer.recognize(img, face.keypoint);

        if (res.id >= 0) {
            Serial1.printf("OPEN:%d\n", res.id);
            Serial.printf("[RECOG] MATCH  id=%d  similarity=%.3f\n",
                          res.id, res.similarity);
        }
        else {
            Serial1.println("DENIED");
            Serial.printf("[RECOG] NO MATCH  similarity=%.3f\n", res.similarity);
        }
    }

    esp_camera_fb_return(fb);
}

// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.println("\n[SYS] STM-FaceGuard ESP32-S3 starting...");

    // UART tới STM32
    Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
    rxBuf.reserve(32);

    // Khởi tạo camera
    if (!camera_init()) {
        Serial.println("[SYS] Camera init FAILED — kiem tra ket noi, halting");
        while (true) { delay(1000); }
    }
    Serial.printf("[SYS] Camera OK  PSRAM=%s  free_heap=%u\n",
                  psramFound() ? "yes" : "NO",
                  esp_get_free_heap_size());

    // Hiển thị số khuôn mặt đã đăng ký (nạp từ NVS flash)
    Serial.printf("[SYS] Enrolled faces: %d\n", recognizer.get_enrolled_id_num());

    delay(300);
    Serial1.println("READY");
    Serial.println("[SYS] READY → STM32");
}

void loop()
{
    // ── Đọc UART từ STM32 ────────────────────────────────────────────────────
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            rxBuf.trim();
            if (rxBuf.length() > 0) process_cmd(rxBuf);
            rxBuf = "";
        } else if (c != '\r') {
            rxBuf += c;
        }
    }

    // ── Xử lý một frame camera ───────────────────────────────────────────────
    process_frame();
}
