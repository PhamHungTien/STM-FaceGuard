/*
 * STM-FaceGuard — ESP32-S3 Face Recognition Node
 * ─────────────────────────────────────────────────────────────────────────────
 * Board   : ESP32-S3 N16R8 (Freenove ESP32-S3 WROOM hoặc tương đương)
 * Arduino : ESP32 Arduino core >= 2.0.6
 *   - Board: "ESP32S3 Dev Module"
 *   - USB CDC On Boot: Disabled
 *   - Flash Mode: DIO 80MHz
 *   - Flash Size: 16MB (128Mb)
 *   - PSRAM: OPI PSRAM
 *   - Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
 *   - Upload Mode: UART0 / Hardware CDC
 *
 * Wiring (ESP32-S3 <-> STM32F303RE Nucleo):
 *   GPIO19 (ESP TX) --> PA10 (STM32 USART1_RX)
 *   GPIO20 (ESP RX) <-- PA9  (STM32 USART1_TX)
 *   GND             --- GND
 *   Note: Header pins "TX/RX" on this board are kept free for upload/Serial Monitor.
 *
 * ── Giao thức STM32 → ESP32 ──────────────────────────────────────────────────
 *   "ENROLL\n"   – bắt đầu đăng ký khuôn mặt mới
 *   "DEL_ALL\n"  – xoá toàn bộ khuôn mặt khỏi flash
 *   "CANCEL\n"   – huỷ đăng ký đang diễn ra
 *   "STATUS\n"   – yêu cầu ESP32 gửi lại READY/FACES để đồng bộ trạng thái
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
#include "esp_err.h"
#include "esp_system.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "face_recognition_tool.hpp"
#include "face_recognition_112_v1_s8.hpp"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <WebServer.h>
#include <WiFi.h>

// ── Camera pins — ESP32-S3 N16R8 CAM (OV3660 3MP) ───────────────────────────
// Tích hợp sẵn trên board, không cần nối thêm dây camera
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
#define STM32_TX_PIN      19    // GPIO19 → STM32 PA10 (USART1_RX)
#define STM32_RX_PIN      20    // GPIO20 ← STM32 PA9  (USART1_TX)

// ── Wi-Fi preview (HTTP snapshot view) ───────────────────────────────────────
#define PREVIEW_ENABLE                  1
#define PREVIEW_AP_SSID  "STM-FaceGuard-Preview"
#define PREVIEW_AP_PASS  "faceguard123"
#define PREVIEW_AP_CHANNEL              6
#define PREVIEW_STA_SSID               ""
#define PREVIEW_STA_PASS               ""
#define PREVIEW_REFRESH_MS            700
#define PREVIEW_JPEG_QUALITY           80
#define CAMERA_JPEG_QUALITY            12

// ── Đèn trợ sáng khuôn mặt ───────────────────────────────────────────────────
// Pin flash LED được chỉnh theo pinout board do người dùng cung cấp.
// Nếu board thực tế không có flash LED trên GPIO48, đặt FACE_LIGHT_ENABLE = 0.
#define FACE_LIGHT_ENABLE              1
#define FACE_LIGHT_GPIO               48
#define FACE_LIGHT_ACTIVE_LEVEL     HIGH
#define FACE_LIGHT_IDLE_LEVEL       LOW
#define FACE_LIGHT_HOLD_MS          1200

// ── Tham số tuning ────────────────────────────────────────────────────────────
#define OPEN_COOLDOWN_MS          3000   // ms sau OPEN trước khi nhận diện lại
#define DENIED_COOLDOWN_MS        1500   // ms sau DENIED trước khi thử lại
#define ENROLL_FACE_TIMEOUT_MS   10000   // ms chờ phát hiện khuôn mặt mỗi bước
#define ENROLL_STEP_DELAY_MS      2500   // ms tối thiểu giữ mỗi tư thế (bước 2–5)
#define ENROLL_TOTAL_STEPS           5
#define MAX_ENROLLED_FACES           7   // giới hạn thư viện ESP32 face recognition
#define RX_BUF_MAX_LEN              32   // bảo vệ rxBuf khỏi overflow
#define AUTO_STATUS_BEACON_MS     2000   // phát READY/FACES định kỳ khi rảnh

// ── Ngưỡng nhận diện ──────────────────────────────────────────────────────────
// Giá trị cao hơn = an toàn hơn. Điều chỉnh 0.55–0.65 theo môi trường ánh sáng.
#define RECOGNITION_THRESHOLD     0.60F

// ── Voting: yêu cầu N frame liên tiếp khớp trước khi mở cửa ─────────────────
// Loại bỏ false positive từ 1 frame nhiễu. N=2 thêm ~0.2 s độ trễ (chấp nhận được).
#define REQUIRED_MATCHES             2

// ── Lockout: khoá sau N lần thất bại liên tiếp ────────────────────────────────
#define MAX_FAILURES                 5
#define LOCKOUT_DURATION_MS  (5UL * 60UL * 1000UL)   // 5 phút

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
static int      enrolledId  = -1;    // ID thực tế từ enroll_id() bước FRONT

// ── Face AI objects ───────────────────────────────────────────────────────────
// Detector params: score_threshold, nms_threshold, top_k, min_face_size
static HumanFaceDetectMSR01   detector(0.3F, 0.3F, 10, 0.3F);
static FaceRecognition112V1S8 recognizer;   // tự nạp face DB từ NVS/flash

// ── Misc ──────────────────────────────────────────────────────────────────────
static uint32_t lastOpenMs      = 0;   // cooldown sau OPEN
static uint32_t lastDeniedMs    = 0;   // cooldown sau DENIED
static uint32_t lastStatusTxMs  = 0;   // chống spam READY/FACES khi host hỏi dồn
static uint32_t lastBeaconTxMs  = 0;   // heartbeat để STM32 tự bắt lại đồng bộ
static String   rxBuf;                 // bộ đệm dòng UART từ STM32
static WebServer previewServer(80);
static bool     previewServerStarted = false;
static wl_status_t previewLastStaStatus = WL_IDLE_STATUS;

// ── Voting state ──────────────────────────────────────────────────────────────
static int      matchCount      = 0;   // số frame khớp liên tiếp
static int      lastMatchId     = -1;  // ID đang vote

// ── Lockout state ─────────────────────────────────────────────────────────────
static int      failureCount    = 0;           // lần thất bại liên tiếp
static uint32_t lockoutUntilMs  = 0;           // khóa đến thời điểm này
static esp_err_t cameraInitErr  = ESP_OK;      // lưu lỗi init camera để chẩn đoán
static bool     cameraReady     = false;
static uint32_t lastCamFailTxMs = 0;
static uint8_t *rgbFrameBuf     = nullptr;
static size_t   rgbFrameBufLen  = 0;
static bool     faceLightOn     = false;
static uint32_t faceLightUntilMs = 0;

static const char *reset_reason_name(esp_reset_reason_t reason)
{
    switch (reason) {
        case ESP_RST_POWERON: return "POWERON";
        case ESP_RST_EXT: return "EXT";
        case ESP_RST_SW: return "SW";
        case ESP_RST_PANIC: return "PANIC";
        case ESP_RST_INT_WDT: return "INT_WDT";
        case ESP_RST_TASK_WDT: return "TASK_WDT";
        case ESP_RST_WDT: return "WDT";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
        case ESP_RST_BROWNOUT: return "BROWNOUT";
        case ESP_RST_SDIO: return "SDIO";
        default: return "UNKNOWN";
    }
}

static const char *app_state_name()
{
    return (appState == STATE_ENROLLING) ? "ENROLLING" : "IDLE";
}

static const char *wifi_status_name(wl_status_t status)
{
    switch (status) {
        case WL_IDLE_STATUS: return "IDLE";
        case WL_NO_SSID_AVAIL: return "NO_SSID";
        case WL_SCAN_COMPLETED: return "SCAN_DONE";
        case WL_CONNECTED: return "CONNECTED";
        case WL_CONNECT_FAILED: return "CONNECT_FAILED";
        case WL_CONNECTION_LOST: return "CONNECTION_LOST";
        case WL_DISCONNECTED: return "DISCONNECTED";
        default: return "UNKNOWN";
    }
}

static bool ensure_rgb_frame_buf(size_t width, size_t height)
{
    size_t needed = width * height * 3U;
    if (rgbFrameBuf && rgbFrameBufLen >= needed) {
        return true;
    }

    if (rgbFrameBuf) {
        heap_caps_free(rgbFrameBuf);
        rgbFrameBuf = nullptr;
        rgbFrameBufLen = 0;
    }

    rgbFrameBuf = (uint8_t *)heap_caps_malloc(needed, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgbFrameBuf) {
        rgbFrameBuf = (uint8_t *)heap_caps_malloc(needed, MALLOC_CAP_8BIT);
    }

    if (!rgbFrameBuf) {
        Serial.printf("[SYS] RGB work buffer alloc FAILED (%u bytes)\n", (unsigned)needed);
        return false;
    }

    rgbFrameBufLen = needed;
    Serial.printf("[SYS] RGB work buffer ready (%u bytes)\n", (unsigned)needed);
    return true;
}

static void face_light_apply(bool on)
{
#if FACE_LIGHT_ENABLE
    digitalWrite(FACE_LIGHT_GPIO, on ? FACE_LIGHT_ACTIVE_LEVEL : FACE_LIGHT_IDLE_LEVEL);
#endif
    faceLightOn = on;
}

static void face_light_init()
{
#if FACE_LIGHT_ENABLE
    pinMode(FACE_LIGHT_GPIO, OUTPUT);
#endif
    face_light_apply(false);
}

static void face_light_touch(uint32_t holdMs = FACE_LIGHT_HOLD_MS)
{
    uint32_t until = millis() + holdMs;
    if (!faceLightOn) {
        face_light_apply(true);
        Serial.println("[LIGHT] Face light ON");
    }
    if ((int32_t)(until - faceLightUntilMs) > 0) {
        faceLightUntilMs = until;
    }
}

static void face_light_poll()
{
    if (faceLightOn && (int32_t)(millis() - faceLightUntilMs) >= 0) {
        face_light_apply(false);
        Serial.println("[LIGHT] Face light OFF");
    }
}

static void preview_handle_root()
{
    String html;
    html.reserve(2200);
    html += F(
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>STM-FaceGuard Preview</title>"
        "<style>"
        "body{margin:0;background:#0b1724;color:#eaf2ff;font-family:Helvetica,Arial,sans-serif;}"
        ".wrap{max-width:980px;margin:0 auto;padding:24px;}"
        ".hero{display:grid;grid-template-columns:1.3fr .7fr;gap:18px;align-items:start;}"
        ".card{background:#102235;border:1px solid #27435f;border-radius:18px;padding:18px;"
        "box-shadow:0 12px 32px rgba(0,0,0,.25);}"
        "h1{margin:0 0 8px;font-size:28px;letter-spacing:.02em;}"
        ".sub{margin:0 0 18px;color:#97b7d9;}"
        ".cam{width:100%;aspect-ratio:1/1;object-fit:cover;border-radius:14px;background:#07111c;"
        "border:1px solid #1e3851;}"
        ".grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:10px;}"
        ".pill{display:inline-block;padding:4px 10px;border-radius:999px;background:#16314b;color:#9fd2ff;"
        "font-size:12px;margin-bottom:14px;}"
        ".label{font-size:12px;color:#97b7d9;text-transform:uppercase;letter-spacing:.08em;}"
        ".value{font-size:20px;font-weight:700;margin-top:4px;}"
        ".mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:13px;color:#d4e6ff;}"
        "@media (max-width:780px){.hero{grid-template-columns:1fr;}}"
        "</style></head><body><div class='wrap'>"
        "<span class='pill'>ESP32-S3 Camera Preview</span>"
        "<h1>STM-FaceGuard</h1>"
        "<p class='sub'>Preview dạng ảnh chụp nhanh để giảm tải cho face AI trong lúc debug.</p>"
        "<div class='hero'><div class='card'>"
        "<img id='cam' class='cam' alt='Camera preview' src='/capture'>"
        "</div><div class='card'><div class='grid'>"
        "<div><div class='label'>Camera</div><div class='value' id='camera'>...</div></div>"
        "<div><div class='label'>Faces</div><div class='value' id='faces'>...</div></div>"
        "<div><div class='label'>State</div><div class='value' id='state'>...</div></div>"
        "<div><div class='label'>Failures</div><div class='value' id='failures'>...</div></div>"
        "<div><div class='label'>Lockout</div><div class='value' id='lockout'>...</div></div>"
        "<div><div class='label'>AP URL</div><div class='value mono' id='apurl'>...</div></div>"
        "<div><div class='label'>STA URL</div><div class='value mono' id='staurl'>...</div></div>"
        "</div>"
        "<p class='sub' style='margin-top:16px'>"
        "AP mặc định: <span class='mono'>http://192.168.4.1/</span><br>"
        "SSID: <span class='mono'>");
    html += PREVIEW_AP_SSID;
    html += F("</span> | Password: <span class='mono'>");
    html += PREVIEW_AP_PASS;
    html += F("</span></p>");

    if (strlen(PREVIEW_STA_SSID) > 0) {
        html += F("<p class='sub'>STA target: <span class='mono'>");
        html += PREVIEW_STA_SSID;
        html += F("</span></p>");
    } else {
        html += F("<p class='sub'>STA mode đang tắt. Muốn mở qua router, điền "
                  "<span class='mono'>PREVIEW_STA_SSID/PASS</span> trong firmware.</p>");
    }

    html += F(
        "</div></div></div>"
        "<script>"
        "const img=document.getElementById('cam');"
        "const refreshMs=");
    html += String(PREVIEW_REFRESH_MS);
    html += F(
        ";"
        "function refreshShot(){img.src='/capture?ts='+Date.now();}"
        "function updateStatus(){fetch('/status',{cache:'no-store'})"
        ".then(r=>r.json()).then(s=>{"
        "document.getElementById('camera').textContent=s.cameraReady?'READY':'ERROR';"
        "document.getElementById('faces').textContent=s.faces;"
        "document.getElementById('state').textContent=s.state;"
        "document.getElementById('failures').textContent=s.failures;"
        "document.getElementById('lockout').textContent=s.lockout?'ACTIVE':'CLEAR';"
        "document.getElementById('apurl').textContent=s.apUrl;"
        "document.getElementById('staurl').textContent=s.staConnected?s.staUrl:'-';"
        "}).catch(()=>{});}"
        "refreshShot();updateStatus();"
        "setInterval(refreshShot,refreshMs);"
        "setInterval(updateStatus,1000);"
        "</script></body></html>");

    previewServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    previewServer.send(200, "text/html; charset=utf-8", html);
}

static void preview_handle_status()
{
    String json;
    json.reserve(320);

    const bool staConnected = (WiFi.status() == WL_CONNECTED);
    const bool lockoutActive = (lockoutUntilMs > millis());

    json += F("{\"cameraReady\":");
    json += cameraReady ? "true" : "false";
    json += F(",\"faces\":");
    json += String(recognizer.get_enrolled_id_num());
    json += F(",\"state\":\"");
    json += app_state_name();
    json += F("\",\"failures\":");
    json += String(failureCount);
    json += F(",\"lockout\":");
    json += lockoutActive ? "true" : "false";
    json += F(",\"staConnected\":");
    json += staConnected ? "true" : "false";
    json += F(",\"apUrl\":\"http://");
    json += WiFi.softAPIP().toString();
    json += F("/\",\"staUrl\":\"");
    if (staConnected) {
        json += "http://";
        json += WiFi.localIP().toString();
        json += "/";
    }
    json += F("\"}");

    previewServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    previewServer.send(200, "application/json", json);
}

static void preview_handle_capture()
{
    if (!cameraReady) {
        previewServer.send(503, "text/plain; charset=utf-8", "Camera is not ready");
        return;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        previewServer.send(500, "text/plain; charset=utf-8", "Camera frame unavailable");
        return;
    }

    uint8_t *jpgBuf = nullptr;
    size_t jpgLen = 0;
    bool mustFreeJpg = false;

    if (fb->format == PIXFORMAT_JPEG) {
        jpgBuf = fb->buf;
        jpgLen = fb->len;
    } else {
        if (!frame2jpg(fb, PREVIEW_JPEG_QUALITY, &jpgBuf, &jpgLen)) {
            esp_camera_fb_return(fb);
            previewServer.send(500, "text/plain; charset=utf-8", "JPEG encode failed");
            return;
        }
        mustFreeJpg = true;
    }

    previewServer.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    previewServer.sendHeader("Pragma", "no-cache");
    previewServer.setContentLength(jpgLen);
    previewServer.send(200, "image/jpeg", "");
    previewServer.client().write(jpgBuf, jpgLen);

    if (mustFreeJpg) {
        free(jpgBuf);
    }
    esp_camera_fb_return(fb);
}

static void preview_handle_not_found()
{
    previewServer.send(404, "text/plain; charset=utf-8", "Not found");
}

static void preview_start_server()
{
    if (previewServerStarted) {
        return;
    }

    previewServer.on("/", HTTP_GET, preview_handle_root);
    previewServer.on("/status", HTTP_GET, preview_handle_status);
    previewServer.on("/capture", HTTP_GET, preview_handle_capture);
    previewServer.onNotFound(preview_handle_not_found);
    previewServer.begin();
    previewServerStarted = true;
    Serial.println("[PREVIEW] HTTP server ready");
}

static void preview_start_network()
{
    WiFi.persistent(false);

    const bool wantSta = (strlen(PREVIEW_STA_SSID) > 0);
    WiFi.mode(wantSta ? WIFI_AP_STA : WIFI_AP);
    delay(100);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);

    bool apReady = WiFi.softAP(PREVIEW_AP_SSID, PREVIEW_AP_PASS, PREVIEW_AP_CHANNEL, 0, 2);
    Serial.printf("[PREVIEW] AP %s  SSID=%s  URL=http://%s/\n",
                  apReady ? "OK" : "FAILED",
                  PREVIEW_AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    if (wantSta) {
        WiFi.begin(PREVIEW_STA_SSID, PREVIEW_STA_PASS);
        previewLastStaStatus = WiFi.status();
        Serial.printf("[PREVIEW] STA connect -> SSID=%s\n", PREVIEW_STA_SSID);
    }

    preview_start_server();
}

static void preview_poll()
{
    if (previewServerStarted) {
        previewServer.handleClient();
    }

    if (strlen(PREVIEW_STA_SSID) == 0) {
        return;
    }

    wl_status_t currentStatus = WiFi.status();
    if (currentStatus == previewLastStaStatus) {
        return;
    }

    previewLastStaStatus = currentStatus;
    Serial.printf("[PREVIEW] STA status=%s\n", wifi_status_name(currentStatus));

    if (currentStatus == WL_CONNECTED) {
        Serial.printf("[PREVIEW] STA URL=http://%s/\n", WiFi.localIP().toString().c_str());
    }
}

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
    cfg.pixel_format  = PIXFORMAT_JPEG;      // Wi-Fi + RGB dễ lỗi, chụp JPEG rồi decode cho AI
    cfg.frame_size    = FRAMESIZE_240X240;   // cân bằng tốt cho face AI
    cfg.jpeg_quality  = CAMERA_JPEG_QUALITY;
    cfg.fb_count      = psramFound() ? 2 : 1;
    cfg.fb_location   = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    cfg.grab_mode     = CAMERA_GRAB_WHEN_EMPTY;

    cameraInitErr = esp_camera_init(&cfg);
    if (cameraInitErr != ESP_OK) {
        return false;
    }

    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, 0);
        s->set_hmirror(s, 1);       // gương ngang — nhìn tự nhiên như selfie
        s->set_whitebal(s, 1);      // auto white balance
        s->set_awb_gain(s, 1);      // auto WB gain
        s->set_exposure_ctrl(s, 1); // auto exposure
        s->set_aec2(s, 1);          // AEC algorithm 2 (better in low light)
        s->set_gain_ctrl(s, 1);     // auto gain
        s->set_brightness(s, 1);    // slightly brighter for indoor use (+1)
        s->set_contrast(s, 1);      // slightly higher contrast (+1)
        s->set_saturation(s, 0);    // neutral saturation
        s->set_sharpness(s, 1);     // mild sharpening for face detail
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        cameraInitErr = ESP_FAIL;
        return false;
    }

    bool ok = ensure_rgb_frame_buf(fb->width, fb->height);
    esp_camera_fb_return(fb);
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
static void send_status()
{
    if (!cameraReady) {
        Serial1.printf("CAM_FAIL:%d\n", (int)cameraInitErr);
        return;
    }

    uint32_t now = millis();
    if ((now - lastStatusTxMs) < 100U) {
        return;
    }

    lastStatusTxMs = now;
    Serial1.println("READY");
    Serial1.printf("FACES:%d\n", recognizer.get_enrolled_id_num());

    if (lockoutUntilMs > now) {
        Serial1.println("LOCKOUT");
    } else if (appState == STATE_ENROLLING) {
        Serial1.printf("%s\n", ENROLL_STEPS[enrollStep]);
    }

}

// ─────────────────────────────────────────────────────────────────────────────
// Xử lý lệnh nhận từ STM32
// ─────────────────────────────────────────────────────────────────────────────
static void process_cmd(const String &cmd)
{
    Serial.printf("[CMD] «%s»\n", cmd.c_str());

    if (cmd == "ENROLL") {
        if (appState == STATE_IDLE) {
            if (recognizer.get_enrolled_id_num() >= MAX_ENROLLED_FACES) {
                Serial1.println("DB_FULL");   // notify STM32 immediately
                Serial.println("[ENROLL] DB full → DB_FULL sent");
                return;
            }
            appState    = STATE_ENROLLING;
            enrollStep  = 0;
            stepStartMs = millis();
            Serial1.printf("%s\n", ENROLL_STEPS[0]);
            Serial.printf("[ENROLL] Step 1/%d → %s\n", ENROLL_TOTAL_STEPS, ENROLL_STEPS[0]);
        }
    }
    else if (cmd == "DEL_ALL") {
        recognizer.clear_id();
        appState     = STATE_IDLE;
        failureCount = 0;      // clear failure counter after admin delete
        lockoutUntilMs = 0;    // also clear any active lockout
        Serial1.println("DELETED");
        Serial.println("[SYS] Face DB cleared");
    }
    else if (cmd == "CANCEL") {
        appState   = STATE_IDLE;
        enrolledId = -1;   /* reset in case CANCEL arrives mid-enroll */
        Serial.println("[ENROLL] Cancelled by STM32");
    }
    else if (cmd == "STATUS") {
        send_status();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Xử lý một frame camera: nhận diện hoặc đăng ký
// ─────────────────────────────────────────────────────────────────────────────
static void process_frame()
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) return;

    if (!ensure_rgb_frame_buf(fb->width, fb->height)) {
        esp_camera_fb_return(fb);
        delay(10);
        return;
    }

    if (!fmt2rgb888(fb->buf, fb->len, fb->format, rgbFrameBuf)) {
        Serial.println("[SYS] fmt2rgb888 FAILED");
        esp_camera_fb_return(fb);
        delay(10);
        return;
    }

    // Phát hiện khuôn mặt trên buffer RGB888 đã decode từ JPEG
    std::list<dl::detect::result_t> faces =
        detector.infer(rgbFrameBuf, {(int)fb->height, (int)fb->width, 3});

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

    face_light_touch();

    dl::detect::result_t &face = faces.front();

    // Bọc frame buffer thành Tensor (không copy, trỏ thẳng vào fb->buf)
    Tensor<uint8_t> img;
    img.set_element(rgbFrameBuf)
       .set_shape({(int)fb->height, (int)fb->width, 3})
       .set_auto_free(false);

    // ── Chế độ ĐĂNG KÝ ───────────────────────────────────────────────────────
    if (appState == STATE_ENROLLING)
    {
        if (enrollStep == 0) {
            // Bước FRONT: capture thật ngay khi thấy mặt
            int result = recognizer.enroll_id(img, face.keypoint, "", false);
            if (result < 0) {
                // enroll_id thất bại (không align được mặt), thử lại frame sau
                esp_camera_fb_return(fb);
                return;
            }
            enrolledId = result;  // lưu ID thực để báo STM32 sau khi hoàn tất
            Serial.printf("[ENROLL] Captured FRONT, ID=%d\n", result);
            // Sang bước 1 (LEFT)
            enrollStep++;
            stepStartMs = millis();
            Serial1.printf("%s\n", ENROLL_STEPS[enrollStep]);
            Serial.printf("[ENROLL] Step 2/%d → %s\n", ENROLL_TOTAL_STEPS, ENROLL_STEPS[enrollStep]);
        }
        else {
            // Bước 1–4: chờ đủ ENROLL_STEP_DELAY_MS để người dùng kịp xoay mặt
            if ((millis() - stepStartMs) < ENROLL_STEP_DELAY_MS) {
                esp_camera_fb_return(fb);
                return;
            }

            bool is_last = (enrollStep == ENROLL_TOTAL_STEPS - 1);

            if (is_last) {
                Serial1.printf("ENROLLED:%d\n", enrolledId);
                Serial.printf("[ENROLL] Done! ID=%d  total=%d faces\n",
                              enrolledId, recognizer.get_enrolled_id_num());
                appState   = STATE_IDLE;
                enrolledId = -1;
            }
            else {
                enrollStep++;
                stepStartMs = millis();
                Serial1.printf("%s\n", ENROLL_STEPS[enrollStep]);
                Serial.printf("[ENROLL] Step %d/%d → %s\n",
                              enrollStep + 1, ENROLL_TOTAL_STEPS, ENROLL_STEPS[enrollStep]);
            }
        }
    }
    // ── Chế độ NHẬN DIỆN (IDLE) ───────────────────────────────────────────────
    else
    {
        // Bỏ qua nếu không có mặt nào đăng ký
        if (recognizer.get_enrolled_id_num() == 0) {
            esp_camera_fb_return(fb);
            return;
        }

        uint32_t now = millis();

        // ── Lockout check ─────────────────────────────────────────────────────
        if (now < lockoutUntilMs) {
            esp_camera_fb_return(fb);
            return;
        }

        // ── Cooldown: khi đang trong open cooldown, reset vote và skip ────────
        if ((now - lastOpenMs) < OPEN_COOLDOWN_MS) {
            matchCount  = 0;
            lastMatchId = -1;
            esp_camera_fb_return(fb);
            return;
        }

        // Skip nếu denied cooldown còn hiệu lực
        if ((now - lastDeniedMs) < DENIED_COOLDOWN_MS) {
            esp_camera_fb_return(fb);
            return;
        }

        // ── Nhận diện ─────────────────────────────────────────────────────────
        face_info_t res = recognizer.recognize(img, face.keypoint);

        if (res.id >= 0 && res.similarity >= RECOGNITION_THRESHOLD) {
            // ── VOTING: tích lũy frame khớp liên tiếp ─────────────────────────
            if (res.id == lastMatchId) {
                matchCount++;
            } else {
                matchCount  = 1;
                lastMatchId = res.id;
            }
            Serial.printf("[RECOG] Vote %d/%d  id=%d  sim=%.3f\n",
                          matchCount, REQUIRED_MATCHES, res.id, res.similarity);

            if (matchCount >= REQUIRED_MATCHES) {
                // Xác nhận — mở cửa
                failureCount = 0;      // reset failure counter on success
                matchCount   = 0;
                lastMatchId  = -1;
                lastOpenMs   = now;
                Serial1.printf("OPEN:%d\n", res.id);
                Serial.printf("[RECOG] CONFIRMED  id=%d  sim=%.3f\n",
                              res.id, res.similarity);
            }
        }
        else {
            // Không khớp — reset vote
            matchCount  = 0;
            lastMatchId = -1;

            lastDeniedMs = now;
            failureCount++;
            Serial.printf("[RECOG] NO MATCH  id=%d  sim=%.3f  failures=%d/%d\n",
                          res.id, res.similarity, failureCount, MAX_FAILURES);

            // ── Lockout trigger ───────────────────────────────────────────────
            if (failureCount >= MAX_FAILURES) {
                failureCount   = 0;
                lockoutUntilMs = now + LOCKOUT_DURATION_MS;
                Serial1.println("LOCKOUT");
                Serial.printf("[SECURITY] LOCKOUT for %lu ms\n", LOCKOUT_DURATION_MS);

                // Gửi LOCKOUT_CLEAR sau khi hết thời gian (dùng một lần trigger)
                // — được xử lý bởi lockout_check() trong loop()
            } else {
                Serial1.println("DENIED");
            }
        }
    }

    esp_camera_fb_return(fb);
}

// ─────────────────────────────────────────────────────────────────────────────

void setup()
{
    Serial.begin(115200);
    Serial.printf("\n[SYS] STM-FaceGuard ESP32-S3 starting... reset=%s\n",
                  reset_reason_name(esp_reset_reason()));
    face_light_init();

    // UART tới STM32
    Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
    rxBuf.reserve(32);
    Serial1.println("BOOTING");

    // Khởi tạo camera
    cameraReady = camera_init();
    if (!cameraReady) {
        Serial.printf("[SYS] Camera init FAILED err=0x%04X (%s)\n",
                      (unsigned int)cameraInitErr,
                      esp_err_to_name(cameraInitErr));
    } else {
        Serial.printf("[SYS] Camera OK  PSRAM=%s  free_heap=%u\n",
                      psramFound() ? "yes" : "NO",
                      esp_get_free_heap_size());
    }
    // Hiển thị số khuôn mặt đã đăng ký (nạp từ NVS flash)
    Serial.printf("[SYS] Enrolled faces: %d\n", recognizer.get_enrolled_id_num());

#if PREVIEW_ENABLE
    preview_start_network();
#endif

    // Bật task watchdog — reset ESP32 nếu vòng loop() bị kẹt quá 30 giây
    esp_task_wdt_init(30, true);   // 30 s timeout, panic + reset
    esp_task_wdt_add(NULL);        // theo dõi task hiện tại (loopTask)

    delay(300);
    if (cameraReady) {
        send_status();
    }
}

void loop()
{
    esp_task_wdt_reset();   // reset WDT mỗi vòng loop để chứng minh không bị kẹt

    // ── Kiểm tra hết lockout ──────────────────────────────────────────────────
    if (lockoutUntilMs > 0 && millis() >= lockoutUntilMs) {
        lockoutUntilMs = 0;
        failureCount   = 0;
        matchCount     = 0;
        lastMatchId    = -1;
        Serial1.println("LOCKOUT_CLEAR");
        Serial.println("[SECURITY] Lockout cleared");
    }

    // ── Đọc UART từ STM32 ────────────────────────────────────────────────────
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            rxBuf.trim();
            if (rxBuf.length() > 0) process_cmd(rxBuf);
            rxBuf = "";
        } else if (c != '\r' && rxBuf.length() < RX_BUF_MAX_LEN) {
            rxBuf += c;
        }
    }

#if PREVIEW_ENABLE
    preview_poll();
#endif

    face_light_poll();

    if (!cameraReady) {
        if ((millis() - lastCamFailTxMs) >= 1000U) {
            lastCamFailTxMs = millis();
            Serial1.printf("CAM_FAIL:%d\n", (int)cameraInitErr);
        }
        delay(10);
        return;
    }

    if (appState == STATE_IDLE &&
        (millis() - lastBeaconTxMs) >= AUTO_STATUS_BEACON_MS) {
        lastBeaconTxMs = millis();
        send_status();
    }

    // ── Xử lý một frame camera ───────────────────────────────────────────────
    process_frame();
}
