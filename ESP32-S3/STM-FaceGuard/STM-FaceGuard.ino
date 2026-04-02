// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 Pham Hung Tien et al. -- STM-FaceGuard project
/*
 * STM-FaceGuard - ESP32-S3 Face Recognition Node
 * -----------------------------------------------------------------------------
 * Board   : ESP32-S3 N16R8 (Freenove ESP32-S3 WROOM hoac tuong duong)
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
 * -- Giao thuc STM32 -> ESP32 --------------------------------------------------
 *   "ENROLL\n"   - bat dau dang ky khuon mat moi
 *   "DEL_ALL\n"  - xoa toan bo khuon mat khoi flash
 *   "CANCEL\n"   - huy dang ky dang dien ra
 *   "STATUS\n"   - yeu cau ESP32 gui lai READY/FACES de dong bo trang thai
 *   "REBOOT\n"   - yeu cau ESP32 tu khoi dong lai bang phan mem
 *   "SECURE_HELLO\n" / "SECURE_READY\n"
 *                - bootstrap tuong thich nguoc truoc khi bat secure UART
 *
 * -- Giao thuc ESP32 -> STM32 --------------------------------------------------
 *   "READY\n"          - khoi dong xong
 *   "BOOTING\n"        - ESP32 dang khoi dong lai / moi boot
 *   "OPEN:<id>\n"      - nhan ra khuon mat id
 *   "DENIED\n"         - khong nhan ra
 *   "ENROLLED:<id>\n"  - dang ky thanh cong, ID moi = id
 *   "DELETED\n"        - da xoa toan bo
 *   "CAM_FAIL:<err>\n" - camera init/runtime loi, STM32 co the kich recovery
 *   Khi ca hai ben deu ho tro, payload UART se duoc dong goi dang
 *   !SEQ:CIPHERTEXT:TAG thay vi chuoi van ban tran.
 *   "ENROLL_FRONT\n"   - buoc 1/5: nhin thang
 *   "ENROLL_LEFT\n"    - buoc 2/5: quay trai
 *   "ENROLL_RIGHT\n"   - buoc 3/5: quay phai
 *   "ENROLL_UP\n"      - buoc 4/5: ngang dau
 *   "ENROLL_DOWN\n"    - buoc 5/5: cui dau
 * -----------------------------------------------------------------------------
 */

#include "esp_camera.h"
#include "esp_err.h"
#include "esp_system.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"
#include "face_recognition_112_v1_s8.hpp"
#include "esp_task_wdt.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <stdarg.h>
#include <string.h>
#include <WebServer.h>
#include <WiFi.h>

// -- Camera pins - ESP32-S3 N16R8 CAM (OV3660 3MP) ---------------------------
// Tich hop san tren board, khong can noi them day camera
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

// -- UART toi STM32 ------------------------------------------------------------
#define STM32_BAUD     115200
#define STM32_TX_PIN      19    // GPIO19 -> STM32 PA10 (USART1_RX)
#define STM32_RX_PIN      20    // GPIO20 <- STM32 PA9  (USART1_TX)

// -- Wi-Fi preview (HTTP snapshot view) ---------------------------------------
#define PREVIEW_ENABLE                  1
#define PREVIEW_AP_SSID  "STM-FaceGuard-Preview"
#define PREVIEW_AP_PASS  "faceguard123"
#define PREVIEW_AP_CHANNEL              1
#define PREVIEW_AP_FALLBACK_CHANNEL     6
#define PREVIEW_AP_MAX_CONN             2
#define PREVIEW_AP_ALLOW_OPEN_FALLBACK  1
#define PREVIEW_AP_HEALTHCHECK_MS    5000
#define PREVIEW_AP_RESTART_GAP_MS    3000
#define PREVIEW_STA_ENABLE              0
#define PREVIEW_STA_SSID               ""
#define PREVIEW_STA_PASS               ""
#define PREVIEW_REFRESH_MS            700
#define PREVIEW_JPEG_QUALITY           80
#define CAMERA_JPEG_QUALITY            12

// -- Den tro sang khuon mat ---------------------------------------------------
// Nhieu board ESP32-S3-CAM kieu nay co ca:
// - flash LED trang thuong o GPIO47
// - RGB/NeoPixel o GPIO48
// Ta bat ca hai kieu de bao phu cac bien the board pho bien.
#define FACE_LIGHT_ENABLE              1
#define FACE_LIGHT_DIGITAL_ENABLE      1
#define FACE_LIGHT_DIGITAL_GPIO       47
#define FACE_LIGHT_ACTIVE_LEVEL     HIGH
#define FACE_LIGHT_IDLE_LEVEL       LOW
#define FACE_LIGHT_NEOPIXEL_ENABLE     0
#define FACE_LIGHT_NEOPIXEL_GPIO      48
#define FACE_LIGHT_NEOPIXEL_WHITE     96
#define FACE_LIGHT_STATUS_ENABLE       0
#define FACE_LIGHT_STATUS_GPIO         2
#define FACE_LIGHT_STATUS_ACTIVE_LEVEL LOW
#define FACE_LIGHT_STATUS_IDLE_LEVEL   HIGH
#define FACE_LIGHT_HOLD_MS           250

// -- Tham so tuning ------------------------------------------------------------
#define OPEN_COOLDOWN_MS          2500   // ms sau OPEN truoc khi nhan dien lai
#define DENIED_COOLDOWN_MS         400   // ms sau DENIED truoc khi thu lai
#define ENROLL_FACE_TIMEOUT_MS   12000   // ms cho phat hien khuon mat moi buoc
#define ENROLL_STEP_DELAY_MS      2000   // ms toi thieu giu moi tu the (buoc 2-N, khong dung khi ENROLL_TOTAL_STEPS=1)
#define ENROLL_TOTAL_STEPS           1   // 1 = chi chup mat thang, xong ngay; tang len 5 de yeu cau da goc do
#define MAX_ENROLLED_FACES           7   // gioi han thu vien ESP32 face recognition
#define RX_BUF_MAX_LEN              96   // du cho cho packet UART co xac thuc
#define AUTO_STATUS_BEACON_MS     2000   // phat READY/FACES dinh ky khi ranh
#define LINK_PACKET_MAX_LEN        96
#define LINK_CRYPT_MAX_LEN         32

// -- Nguong nhan dien ----------------------------------------------------------
// 0.42: nhay hon cho in-door, single-pose DB. Tang len 0.55+ neu bi false positive.
#define RECOGNITION_THRESHOLD     0.42F

// -- Voting: yeu cau N frame lien tiep khop truoc khi mo cua -----------------
// N=1: phan hoi ngay lap tuc (1 frame). Tang len 2 neu bi false positive.
#define REQUIRED_MATCHES             1

// -- Negative voting: chi DENIED sau N frame lien tiep khong khop ------------
#define REQUIRED_NO_MATCHES          3

// -- Lockout: khoa sau N lan that bai lien tiep --------------------------------
#define MAX_FAILURES                 5
#define LOCKOUT_DURATION_MS  (5UL * 60UL * 1000UL)   // 5 phut
#define ENROLL_PROMPT_COUNT          5

#if (ENROLL_TOTAL_STEPS < 1) || (ENROLL_TOTAL_STEPS > ENROLL_PROMPT_COUNT)
#error "ENROLL_TOTAL_STEPS must be between 1 and 5"
#endif

// -- Chuoi prompt gui STM32 khi dang ky ---------------------------------------
static const char * const ENROLL_STEPS[ENROLL_PROMPT_COUNT] = {
    "ENROLL_FRONT",   // buoc 1: nhin thang
    "ENROLL_LEFT",    // buoc 2: quay trai
    "ENROLL_RIGHT",   // buoc 3: quay phai
    "ENROLL_UP",      // buoc 4: ngang dau
    "ENROLL_DOWN"     // buoc 5: cui dau
};

// -- State machine -------------------------------------------------------------
enum AppState { STATE_IDLE, STATE_ENROLLING };
static AppState appState    = STATE_IDLE;
static int      enrollStep  = 0;     // buoc hien tai 0..4
static uint32_t stepStartMs = 0;     // millis() luc bat dau buoc
static int      enrolledId  = -1;    // ID thuc te tu enroll_id() buoc FRONT
static int      step0FailCount   = 0;   // consecutive enroll_id() failures at step 0
static int      step0StableCount = 0;   // consecutive frames with face detected (stability gate)

// -- Face AI objects -----------------------------------------------------------
// Two-stage detection: MSR01 (stage 1 - detect regions) + MNP01 (stage 2 - refine keypoints)
// Keypoints chinh xac tu stage 2 la yeu to quyet dinh recognition similarity.
// Stage 1: score_threshold=0.12, nms=0.45, top_k=10, min_face_size=0.08 - nhay hon cho demo
// Stage 2: score_threshold=0.45, nms=0.3,  top_k=5
static HumanFaceDetectMSR01   s1(0.12F, 0.45F, 10, 0.08F);
static HumanFaceDetectMNP01   s2(0.45F, 0.3F, 5);
static FaceRecognition112V1S8 recognizer;   // tu nap face DB tu NVS/flash

// -- Misc ----------------------------------------------------------------------
static uint32_t lastOpenMs      = 0;   // cooldown sau OPEN
static uint32_t lastDeniedMs    = 0;   // cooldown sau DENIED
static uint32_t lastStatusTxMs  = 0;   // chong spam READY/FACES khi host hoi don
static uint32_t lastBeaconTxMs  = 0;   // heartbeat de STM32 tu bat lai dong bo
static String   rxBuf;                 // bo dem dong UART tu STM32
static bool     linkSecureActive = false;
static uint8_t  linkTxSeq = 1;
static uint8_t  linkRxSeq = 0;
static bool     linkRxSynced = false;
static WebServer previewServer(80);
static bool     previewServerStarted = false;
static wl_status_t previewLastStaStatus = WL_IDLE_STATUS;
static bool     previewApReady = false;
static uint32_t previewLastApCheckMs = 0;
static uint32_t previewLastApRestartMs = 0;

// -- Voting state --------------------------------------------------------------
static int      matchCount      = 0;   // so frame khop lien tiep
static int      lastMatchId     = -1;  // ID dang vote
static int      noMatchCount    = 0;   // so frame lien tiep khong khop

// -- Lockout state -------------------------------------------------------------
static int      failureCount    = 0;           // lan that bai lien tiep
static uint32_t lockoutUntilMs  = 0;           // khoa den thoi diem nay

// -- Web UI - event tracking ---------------------------------------------------
static char     lastEventStr[12] = "";   // "OPEN","DENIED","ENROLLED","DELETED","LOCKOUT"
static int      lastEventIdWeb   = -1;   // face ID cho OPEN / ENROLLED
static float    lastEventSim     = 0.0F; // similarity cua lan nhan dien gan nhat
static uint32_t lastEventSeq     = 0;    // dem tang moi su kien; JS dung de phat hien thay doi
static esp_err_t cameraInitErr  = ESP_OK;      // luu loi init camera de chan doan
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

static void record_event(const char *type, int id = -1, float sim = 0.0F)
{
    strncpy(lastEventStr, type, sizeof(lastEventStr) - 1);
    lastEventStr[sizeof(lastEventStr) - 1] = '\0';
    lastEventIdWeb = id;
    lastEventSim   = sim;
    lastEventSeq++;
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

static const uint32_t linkKeyEnc[4] = {
    0x81B4D3A7UL, 0x6F128C5EUL, 0x27E9B041UL, 0xC35A719DUL
};

static const uint32_t linkKeyMac[4] = {
    0x14C2A96BUL, 0x8D37F050UL, 0x53AE1C84UL, 0xF60B4271UL
};

static uint32_t linkReadBE32(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) |
           ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) |
           (uint32_t)src[3];
}

static void linkWriteBE32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value >> 24);
    dst[1] = (uint8_t)(value >> 16);
    dst[2] = (uint8_t)(value >> 8);
    dst[3] = (uint8_t)value;
}

static char linkHexDigit(uint8_t value)
{
    value &= 0x0FU;
    return (value < 10U) ? (char)('0' + value) : (char)('A' + (value - 10U));
}

static int8_t linkParseHex(char c)
{
    if (c >= '0' && c <= '9') return (int8_t)(c - '0');
    if (c >= 'A' && c <= 'F') return (int8_t)(c - 'A' + 10);
    if (c >= 'a' && c <= 'f') return (int8_t)(c - 'a' + 10);
    return -1;
}

static void linkBytesToHex(const uint8_t *src, size_t len, char *dst)
{
    for (size_t i = 0; i < len; ++i) {
        dst[i * 2U] = linkHexDigit((uint8_t)(src[i] >> 4));
        dst[i * 2U + 1U] = linkHexDigit(src[i]);
    }
    dst[len * 2U] = '\0';
}

static bool linkHexToBytes(const char *src, size_t hexLen, uint8_t *dst)
{
    if (hexLen == 0U || (hexLen & 1U) != 0U) return false;

    for (size_t i = 0; i < hexLen; i += 2U) {
        int8_t hi = linkParseHex(src[i]);
        int8_t lo = linkParseHex(src[i + 1U]);

        if (hi < 0 || lo < 0) return false;
        dst[i / 2U] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    }

    return true;
}

static void linkXteaEncrypt(uint32_t *v0, uint32_t *v1, const uint32_t key[4])
{
    uint32_t a = *v0;
    uint32_t b = *v1;
    uint32_t sum = 0U;

    for (uint8_t round = 0U; round < 32U; ++round) {
        a += (((b << 4) ^ (b >> 5)) + b) ^ (sum + key[sum & 3U]);
        sum += 0x9E3779B9UL;
        b += (((a << 4) ^ (a >> 5)) + a) ^ (sum + key[(sum >> 11) & 3U]);
    }

    *v0 = a;
    *v1 = b;
}

static void linkCrypt(uint8_t seq, const uint8_t *in, size_t len, uint8_t *out)
{
    size_t offset = 0U;
    uint8_t block = 0U;

    while (offset < len) {
        uint32_t v0 = 0x53544647UL ^ ((uint32_t)seq << 24) ^ block;
        uint32_t v1 = 0x4C4E4B31UL ^ ((uint32_t)len << 16) ^ 0x9E3779B9UL;
        uint8_t keystream[8];
        size_t chunk = len - offset;

        if (chunk > 8U) chunk = 8U;

        linkXteaEncrypt(&v0, &v1, linkKeyEnc);
        linkWriteBE32(&keystream[0], v0);
        linkWriteBE32(&keystream[4], v1);

        for (size_t i = 0; i < chunk; ++i) {
            out[offset + i] = (uint8_t)(in[offset + i] ^ keystream[i]);
        }

        offset += chunk;
        ++block;
    }
}

static uint64_t linkMac(uint8_t seq, const uint8_t *data, size_t len)
{
    uint32_t v0 = 0x4D414331UL ^ (uint32_t)seq;
    uint32_t v1 = 0x53544647UL ^ (uint32_t)len;
    size_t offset = 0U;

    linkXteaEncrypt(&v0, &v1, linkKeyMac);

    while (offset < len) {
        uint8_t block[8] = {0};
        size_t chunk = len - offset;

        if (chunk > 8U) chunk = 8U;
        memcpy(block, data + offset, chunk);
        v0 ^= linkReadBE32(&block[0]);
        v1 ^= linkReadBE32(&block[4]);
        linkXteaEncrypt(&v0, &v1, linkKeyMac);
        offset += chunk;
    }

    return (((uint64_t)v0) << 32) | (uint64_t)v1;
}

static bool linkShouldResync(uint8_t seq, const char *plain)
{
    if (seq != 1U) return false;

    return (strcmp(plain, "STATUS") == 0) ||
           (strcmp(plain, "SECURE_HELLO") == 0) ||
           (strcmp(plain, "SECURE_READY") == 0);
}

static bool linkAcceptSeq(uint8_t seq, const char *plain)
{
    if (!linkRxSynced) {
        linkRxSynced = true;
        linkRxSeq = seq;
        return true;
    }

    if (seq == linkRxSeq) {
        if (linkShouldResync(seq, plain)) {
            linkRxSeq = seq;
            return true;
        }
        return false;
    }

    if (linkShouldResync(seq, plain)) {
        linkRxSeq = seq;
        return true;
    }

    if ((uint8_t)(seq - linkRxSeq) <= 128U) {
        linkRxSeq = seq;
        return true;
    }

    return false;
}

static void linkSendPlain(const char *msg)
{
    Serial1.print(msg);
    Serial1.write('\n');
}

static void linkEnableSecure()
{
    linkSecureActive = true;
}

static void linkSend(const char *msg)
{
    size_t plainLen = strlen(msg);

    if (plainLen == 0U) return;

    if (linkSecureActive) {
        char packet[LINK_PACKET_MAX_LEN];
        uint8_t cipher[LINK_CRYPT_MAX_LEN];
        char cipherHex[(LINK_CRYPT_MAX_LEN * 2U) + 1U];
        uint8_t seq = linkTxSeq;
        uint64_t tag;
        int written;

        if (plainLen > LINK_CRYPT_MAX_LEN) {
            Serial.printf("[UART] Plaintext too long for secure packet: %u\n", (unsigned)plainLen);
            return;
        }

        if (seq == 0U) seq = 1U;
        linkTxSeq = (uint8_t)(seq + 1U);
        if (linkTxSeq == 0U) linkTxSeq = 1U;

        linkCrypt(seq, (const uint8_t *)msg, plainLen, cipher);
        linkBytesToHex(cipher, plainLen, cipherHex);
        tag = linkMac(seq, cipher, plainLen);

        written = snprintf(packet, sizeof(packet),
                           "!%02X:%s:%08lX%08lX\n",
                           seq,
                           cipherHex,
                           (unsigned long)(tag >> 32),
                           (unsigned long)(tag & 0xFFFFFFFFUL));
        if (written > 0 && (size_t)written < sizeof(packet)) {
            Serial1.print(packet);
            return;
        }
        Serial.println("[UART] Secure packet build failed, falling back to plaintext");
    }

    linkSendPlain(msg);
}

static void linkSendf(const char *fmt, ...)
{
    char plain[LINK_CRYPT_MAX_LEN + 1U];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(plain, sizeof(plain), fmt, ap);
    va_end(ap);
    linkSend(plain);
}

static bool linkDecodeLine(const String &line, char *out, size_t outSize)
{
    size_t lineLen = (size_t)line.length();

    if (lineLen == 0U || outSize == 0U) return false;

    if (line[0] != '!') {
        size_t copyLen = (lineLen < (outSize - 1U)) ? lineLen : (outSize - 1U);
        memcpy(out, line.c_str(), copyLen);
        out[copyLen] = '\0';
        return true;
    }

    if (lineLen < 22U || line[3] != ':') return false;

    int8_t hi = linkParseHex(line[1]);
    int8_t lo = linkParseHex(line[2]);
    int tagSep = line.lastIndexOf(':');
    uint8_t seq;
    size_t cipherHexLen;
    size_t cipherLen;
    uint8_t cipher[LINK_CRYPT_MAX_LEN];
    uint8_t plain[LINK_CRYPT_MAX_LEN + 1U];
    uint64_t rxTag = 0U;

    if (hi < 0 || lo < 0 || tagSep < 4) return false;
    if ((lineLen - (size_t)tagSep - 1U) != 16U) return false;

    seq = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)lo);
    cipherHexLen = (size_t)tagSep - 4U;
    cipherLen = cipherHexLen / 2U;

    if (cipherLen == 0U || cipherLen >= outSize || cipherLen > LINK_CRYPT_MAX_LEN) return false;
    if (!linkHexToBytes(line.c_str() + 4, cipherHexLen, cipher)) return false;

    for (size_t i = 0; i < 16U; ++i) {
        int8_t nibble = linkParseHex(line[(size_t)tagSep + 1U + i]);
        if (nibble < 0) return false;
        rxTag = (rxTag << 4) | (uint64_t)(uint8_t)nibble;
    }

    if (linkMac(seq, cipher, cipherLen) != rxTag) return false;

    linkCrypt(seq, cipher, cipherLen, plain);
    plain[cipherLen] = '\0';

    if (!linkAcceptSeq(seq, (const char *)plain)) return false;

    memcpy(out, plain, cipherLen + 1U);
    linkEnableSecure();
    return true;
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
  #if FACE_LIGHT_DIGITAL_ENABLE
    digitalWrite(FACE_LIGHT_DIGITAL_GPIO, on ? FACE_LIGHT_ACTIVE_LEVEL : FACE_LIGHT_IDLE_LEVEL);
  #endif
  #if FACE_LIGHT_NEOPIXEL_ENABLE
    if (on) {
        neopixelWrite(FACE_LIGHT_NEOPIXEL_GPIO,
                      FACE_LIGHT_NEOPIXEL_WHITE,
                      FACE_LIGHT_NEOPIXEL_WHITE,
                      FACE_LIGHT_NEOPIXEL_WHITE);
    } else {
        neopixelWrite(FACE_LIGHT_NEOPIXEL_GPIO, 0, 0, 0);
    }
  #endif
  #if FACE_LIGHT_STATUS_ENABLE
    digitalWrite(FACE_LIGHT_STATUS_GPIO, on ? FACE_LIGHT_STATUS_ACTIVE_LEVEL
                                            : FACE_LIGHT_STATUS_IDLE_LEVEL);
  #endif
#endif
    faceLightOn = on;
}

static void face_light_init()
{
#if FACE_LIGHT_ENABLE
  #if FACE_LIGHT_DIGITAL_ENABLE
    pinMode(FACE_LIGHT_DIGITAL_GPIO, OUTPUT);
  #endif
  #if FACE_LIGHT_STATUS_ENABLE
    pinMode(FACE_LIGHT_STATUS_GPIO, OUTPUT);
  #endif
#endif
    face_light_apply(false);
}

static void face_light_self_test()
{
#if FACE_LIGHT_ENABLE
    Serial.println("[LIGHT] Self-test GPIO47");
  #if FACE_LIGHT_DIGITAL_ENABLE
    digitalWrite(FACE_LIGHT_DIGITAL_GPIO, FACE_LIGHT_ACTIVE_LEVEL);
  #endif
    delay(250);
  #if FACE_LIGHT_DIGITAL_ENABLE
    digitalWrite(FACE_LIGHT_DIGITAL_GPIO, FACE_LIGHT_IDLE_LEVEL);
  #endif

    Serial.println("[LIGHT] Self-test GPIO48");
  #if FACE_LIGHT_NEOPIXEL_ENABLE
    neopixelWrite(FACE_LIGHT_NEOPIXEL_GPIO,
                  FACE_LIGHT_NEOPIXEL_WHITE,
                  FACE_LIGHT_NEOPIXEL_WHITE,
                  FACE_LIGHT_NEOPIXEL_WHITE);
    delay(250);
    neopixelWrite(FACE_LIGHT_NEOPIXEL_GPIO, 0, 0, 0);
  #endif

    Serial.println("[LIGHT] Self-test GPIO2");
  #if FACE_LIGHT_STATUS_ENABLE
    digitalWrite(FACE_LIGHT_STATUS_GPIO, FACE_LIGHT_STATUS_ACTIVE_LEVEL);
    delay(250);
    digitalWrite(FACE_LIGHT_STATUS_GPIO, FACE_LIGHT_STATUS_IDLE_LEVEL);
  #endif

    Serial.println("[LIGHT] Self-test OFF");
#endif
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
    html.reserve(5000);
    html += F("<!doctype html><html><head><meta charset='utf-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>STM-FaceGuard</title><style>"
              "*{box-sizing:border-box}"
              "body{margin:0;background:#0b1724;color:#eaf2ff;font-family:Helvetica,Arial,sans-serif}"
              ".wrap{max-width:980px;margin:0 auto;padding:20px}"
              ".hero{display:grid;grid-template-columns:1fr 1fr;gap:16px;align-items:start}"
              ".card{background:#102235;border:1px solid #27435f;border-radius:16px;padding:16px}"
              "h1{margin:0 0 4px;font-size:26px}"
              ".sub{margin:0 0 14px;color:#97b7d9;font-size:13px}"
              ".pill{display:inline-block;padding:3px 10px;border-radius:999px;background:#16314b;"
              "color:#9fd2ff;font-size:11px;margin-bottom:10px}"
              ".cam{width:100%;aspect-ratio:1/1;object-fit:cover;border-radius:12px;"
              "background:#07111c;border:1px solid #1e3851;display:block}"
              ".grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:12px}"
              ".cell{padding:8px 10px;background:#0d1c2e;border-radius:10px}"
              ".lbl{font-size:10px;color:#97b7d9;text-transform:uppercase;letter-spacing:.06em}"
              ".val{font-size:18px;font-weight:700;margin-top:2px;transition:color .3s}"
              ".evbox{background:#0d1c2e;border-radius:10px;padding:8px;min-height:64px}"
              ".ev{font-size:12px;padding:4px 0;border-bottom:1px solid #1a304a;display:flex;"
              "justify-content:space-between;align-items:center}"
              ".ev:last-child{border-bottom:none}"
              ".evt{font-size:10px;color:#4a6a8a}"
              ".info{display:flex;gap:14px;font-size:11px;color:#4a6a8a;margin-top:10px;flex-wrap:wrap}"
              ".mono{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px}"
              ".btn{display:block;width:100%;padding:12px;border:none;border-radius:10px;"
              "cursor:pointer;font-size:15px;font-weight:700;margin-top:12px;transition:opacity .15s}"
              ".btn:active{opacity:.7}"
              ".btn-unlock{background:#22c55e;color:#fff}"
              ".btn-unlock:disabled{background:#1e3a2a;color:#3a7a50;cursor:not-allowed}"
              "@media(max-width:680px){.hero{grid-template-columns:1fr}}"
              "</style></head><body><div class='wrap'>"
              "<span class='pill'>ESP32-S3 Face Recognition</span>"
              "<h1>STM-FaceGuard</h1>"
              "<p class='sub'>SSID: <span class='mono'>");
    html += PREVIEW_AP_SSID;
    html += F("</span>&nbsp;&nbsp;Pass: <span class='mono'>");
    html += PREVIEW_AP_PASS;
    html += F("</span></p>"
              "<div class='hero'>"
              "<div class='card'>"
              "<img id='cam' class='cam' src='/capture' alt=''>"
              "</div>"
              "<div class='card'>"
              "<div class='grid'>"
              "<div class='cell'><div class='lbl'>Camera</div><div class='val' id='vcam'>…</div></div>"
              "<div class='cell'><div class='lbl'>Khuôn mặt</div><div class='val' id='vfaces'>…</div></div>"
              "<div class='cell'><div class='lbl'>Trạng thái</div><div class='val' id='vstate'>…</div></div>"
              "<div class='cell'><div class='lbl'>Khóa</div><div class='val' id='vlockout'>…</div></div>"
              "</div>"
              "<div class='lbl' style='margin-bottom:6px'>Lịch sử sự kiện</div>"
              "<div class='evbox' id='evlog'>"
              "<div class='ev'><span style='color:#4a6a8a'>Chưa có sự kiện</span></div>"
              "</div>"
              "<button class='btn btn-unlock' id='btnUnlock' onclick='doUnlock()'>🔓 Mở khóa từ xa</button>"
              "<div class='info'>"
              "<span>Uptime: <b id='vuptime'>…</b>s</span>"
              "<span>Heap: <b id='vheap'>…</b> KB</span>"
              "<span>Lỗi: <b id='vfail'>…</b></span>"
              "<span class='mono' id='vapurl'></span>"
              "</div></div></div>");

    html += F("<script>"
              "const EV_COL={OPEN:'#4ade80',DENIED:'#f87171',ENROLLED:'#22d3ee',"
              "DELETED:'#fb923c',LOCKOUT:'#f87171'};"
              "let evSeq=-1,evLog=[],lockRemain=0,lockTimer=null;"
              "function fmtT(d){return d.toLocaleTimeString('vi-VN',{hour:'2-digit',"
              "minute:'2-digit',second:'2-digit'});}"
              "function sv(id,txt,col){const e=document.getElementById(id);"
              "e.textContent=txt;e.style.color=col||'';}"
              "function tickLock(){"
              "lockRemain--;if(lockRemain<=0){clearInterval(lockTimer);lockTimer=null;"
              "lockRemain=0;sv('vlockout','Bình thường','#4ade80');}"
              "else sv('vlockout','🔒 '+lockRemain+'s','#f87171');}"
              "function setLock(sec){"
              "lockRemain=sec;"
              "if(lockTimer){clearInterval(lockTimer);lockTimer=null;}"
              "if(sec>0){sv('vlockout','🔒 '+sec+'s','#f87171');"
              "lockTimer=setInterval(tickLock,1000);}"
              "else sv('vlockout','Bình thường','#4ade80');}"
              "function updateStatus(){"
              "fetch('/status',{cache:'no-store'}).then(r=>r.json()).then(s=>{"
              "sv('vcam',s.cameraReady?'OK':'LỖI',s.cameraReady?'#4ade80':'#f87171');"
              "sv('vfaces',s.faces,'#eaf2ff');"
              "sv('vfail',s.failures,'#eaf2ff');"
              "let sc='#4ade80';"
              "if(s.lockout)sc='#f87171';"
              "else if(s.state==='ENROLLING')sc='#fbbf24';"
              "sv('vstate',s.state,sc);"
              "if(s.lockoutRemainSec>0&&s.lockoutRemainSec>lockRemain)setLock(s.lockoutRemainSec);"
              "else if(!s.lockout&&lockRemain<=0)sv('vlockout','Bình thường','#4ade80');"
              "document.getElementById('vapurl').textContent=s.apUrl;"
              "sv('vuptime',s.uptimeSec);sv('vheap',s.heapFreeKB);"
              "if(s.lastEventSeq!==evSeq&&s.lastEvent){"
              "evSeq=s.lastEventSeq;"
              "const col=EV_COL[s.lastEvent]||'#eaf2ff';"
              "const idS=s.lastEventId>=0?' #'+s.lastEventId:'';"
              "const simS=parseFloat(s.lastSim)>0?' ('+parseFloat(s.lastSim).toFixed(2)+')':(s.lastEvent==='DENIED'?' ('+parseFloat(s.lastSim).toFixed(2)+')':'');"
              "evLog.unshift({t:fmtT(new Date()),ev:s.lastEvent+idS+simS,c:col});"
              "if(evLog.length>6)evLog.pop();"
              "document.getElementById('evlog').innerHTML=evLog.map(e=>"
              "'<div class=\"ev\"><b style=\"color:'+e.c+'\">'+e.ev+'</b>"
              "<span class=\"evt\">'+e.t+'</span></div>').join('');"
              "}}).catch(()=>{});}"
              "function doUnlock(){"
              "const btn=document.getElementById('btnUnlock');"
              "btn.disabled=true;btn.textContent='Đang mở...';"
              "fetch('/unlock',{method:'POST',cache:'no-store'})"
              ".then(r=>r.json())"
              ".then(d=>{"
              "btn.textContent=d.ok?'✓ Đã mở cửa!':'✗ '+(d.error||'Lỗi');"
              "setTimeout(()=>{btn.disabled=false;btn.textContent='🔓 Mở khóa từ xa';},2500);"
              "updateStatus();"
              "}).catch(()=>{btn.disabled=false;btn.textContent='🔓 Mở khóa từ xa';});}"
              "const camImg=document.getElementById('cam');"
              "function refreshCam(){camImg.src='/capture?t='+Date.now();}"
              "refreshCam();updateStatus();"
              "setInterval(refreshCam,");
    html += String(PREVIEW_REFRESH_MS);
    html += F(");setInterval(updateStatus,1000);"
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

    // -- Event tracking fields --------------------------------------------------
    json += F("\",\"lastEvent\":\"");
    json += lastEventStr;
    json += F("\",\"lastEventId\":");
    json += String(lastEventIdWeb);
    json += F(",\"lastSim\":");
    char simBuf[8];
    snprintf(simBuf, sizeof(simBuf), "%.3f", lastEventSim);
    json += simBuf;
    json += F(",\"lastEventSeq\":");
    json += String(lastEventSeq);
    json += F(",\"lockoutRemainSec\":");
    json += String(lockoutActive ? (uint32_t)((lockoutUntilMs - millis()) / 1000UL) : 0U);
    json += F(",\"uptimeSec\":");
    json += String(millis() / 1000UL);
    json += F(",\"heapFreeKB\":");
    json += String(esp_get_free_heap_size() / 1024U);
    json += F("}");

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

static void preview_handle_unlock()
{
    if (previewServer.method() != HTTP_POST) {
        previewServer.send(405, "text/plain; charset=utf-8", "POST required");
        return;
    }
    uint32_t now = millis();
    if (now < lockoutUntilMs) {
        previewServer.sendHeader("Cache-Control", "no-store");
        previewServer.send(403, "application/json", "{\"ok\":false,\"error\":\"LOCKOUT\"}");
        return;
    }
    linkSend("OPEN:0");
    lastOpenMs = now;
    record_event("OPEN", 0, 1.0F);
    Serial.println("[WEB] Remote unlock triggered");
    previewServer.sendHeader("Cache-Control", "no-store");
    previewServer.send(200, "application/json", "{\"ok\":true}");
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
    previewServer.on("/unlock", HTTP_POST, preview_handle_unlock);
    previewServer.onNotFound(preview_handle_not_found);
    previewServer.begin();
    previewServerStarted = true;
    Serial.println("[PREVIEW] HTTP server ready");
}

static bool preview_start_ap_try(int channel, bool openMode)
{
    bool apReady = openMode
        ? WiFi.softAP(PREVIEW_AP_SSID, nullptr, channel, 0, PREVIEW_AP_MAX_CONN)
        : WiFi.softAP(PREVIEW_AP_SSID, PREVIEW_AP_PASS, channel, 0, PREVIEW_AP_MAX_CONN);

    if (apReady) {
        // Uu tien tuong thich client: b/g/n + HT20, TX power toi da.
        esp_err_t protocolErr = esp_wifi_set_protocol(
            WIFI_IF_AP,
            WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N
        );
        esp_err_t bwErr = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        esp_err_t txErr = esp_wifi_set_max_tx_power(80);  // 20 dBm theo bang mapping IDF

        if (protocolErr != ESP_OK || bwErr != ESP_OK || txErr != ESP_OK) {
            Serial.printf("[PREVIEW] AP radio tune warning  protocol=%s  bw=%s  tx=%s\n",
                          esp_err_to_name(protocolErr),
                          esp_err_to_name(bwErr),
                          esp_err_to_name(txErr));
        }
    }

    Serial.printf("[PREVIEW] AP %s  mode=%s  ch=%d  SSID=%s  URL=http://%s/\n",
                  apReady ? "OK" : "FAILED",
                  openMode ? "OPEN" : "WPA2",
                  channel,
                  PREVIEW_AP_SSID,
                  WiFi.softAPIP().toString().c_str());
    return apReady;
}

static bool preview_start_ap_with_fallback()
{
    bool apReady = preview_start_ap_try(PREVIEW_AP_CHANNEL, false);
    if (!apReady && PREVIEW_AP_FALLBACK_CHANNEL != PREVIEW_AP_CHANNEL) {
        apReady = preview_start_ap_try(PREVIEW_AP_FALLBACK_CHANNEL, false);
    }
#if PREVIEW_AP_ALLOW_OPEN_FALLBACK
    if (!apReady) {
        apReady = preview_start_ap_try(PREVIEW_AP_FALLBACK_CHANNEL, true);
    }
#endif
    previewApReady = apReady;
    return apReady;
}

static void preview_start_network()
{
    WiFi.persistent(false);
    WiFi.disconnect(true, true);
    delay(100);
    WiFi.mode(WIFI_MODE_NULL);
    delay(100);

    // De on dinh preview AP, mac dinh tat AP+STA (STA co the ep AP doi channel).
    const bool wantSta = (PREVIEW_STA_ENABLE != 0) && (strlen(PREVIEW_STA_SSID) > 0);
    WiFi.mode(wantSta ? WIFI_AP_STA : WIFI_AP);
    delay(100);

    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);

    bool apReady = preview_start_ap_with_fallback();
    if (!apReady) {
        Serial.println("[PREVIEW] AP FAILED in all fallback modes");
    }

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
    esp_task_wdt_reset(); /* handleClient() co the block lau khi co client HTTP */

    uint32_t now = millis();
    if ((now - previewLastApCheckMs) >= PREVIEW_AP_HEALTHCHECK_MS) {
        previewLastApCheckMs = now;
        bool apSeemsDown = !previewApReady || (WiFi.softAPIP()[0] == 0);
        if (apSeemsDown && ((now - previewLastApRestartMs) >= PREVIEW_AP_RESTART_GAP_MS)) {
            previewLastApRestartMs = now;
            Serial.println("[PREVIEW] AP healthcheck failed -> restarting AP");
            WiFi.softAPdisconnect(true);
            esp_task_wdt_reset(); /* WiFi teardown/restart co the mat vai tram ms */
            delay(60);
            preview_start_ap_with_fallback();
            esp_task_wdt_reset();
        }
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

// -----------------------------------------------------------------------------
// Khoi tao camera
// -----------------------------------------------------------------------------
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
    cfg.pixel_format  = PIXFORMAT_RGB565;    // fmt2rgb888(JPEG) co bug silent-corrupt; RGB565->RGB888 on dinh
    cfg.frame_size    = FRAMESIZE_240X240;   // can bang tot cho face AI
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
        s->set_hmirror(s, 1);       // guong ngang - nhin tu nhien nhu selfie
        s->set_whitebal(s, 1);      // auto white balance
        s->set_awb_gain(s, 1);      // auto WB gain
        s->set_exposure_ctrl(s, 1); // auto exposure
        s->set_aec2(s, 1);          // AEC algorithm 2 (better in low light)
        s->set_ae_level(s, 1);      // tang nhe exposure target - sang hon cho in-door demo
        s->set_gain_ctrl(s, 1);     // auto gain
        s->set_agc_gain(s, 0);      // bat dau voi gain thap, de AEC tu dieu chinh
        s->set_brightness(s, 2);    // sang hon cho demo in-door (+2)
        s->set_contrast(s, 1);      // slightly higher contrast (+1)
        s->set_saturation(s, 0);    // neutral saturation
        s->set_sharpness(s, 2);     // ro net hon cho face detail (+2)
        s->set_denoise(s, 1);       // giam nhieu - tot cho moi truong toi
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

// -----------------------------------------------------------------------------
static void send_status(bool includeConfig)
{
    if (!cameraReady) {
        linkSendf("CAM_FAIL:%d", (int)cameraInitErr);
        return;
    }

    uint32_t now = millis();
    if ((now - lastStatusTxMs) < 100U) {
        return;
    }

    lastStatusTxMs = now;

    if (appState == STATE_ENROLLING) {
        linkSend(ENROLL_STEPS[enrollStep]);
        return;
    }

    linkSend("READY");
    linkSendf("FACES:%d", recognizer.get_enrolled_id_num());
    if (includeConfig) {
        linkSendf("ENROLL_CFG:%d", ENROLL_TOTAL_STEPS);
    }

    if (lockoutUntilMs > now) {
        linkSend("LOCKOUT");
    }

}

static void abort_enroll(bool rollback_partial)
{
    if (rollback_partial && appState == STATE_ENROLLING && enrolledId >= 0) {
        int removed = recognizer.delete_id(enrolledId, true);
        Serial.printf("[ENROLL] Rollback partial ID=%d (remaining=%d)\n", enrolledId, removed);
    }

    appState          = STATE_IDLE;
    enrollStep        = 0;
    stepStartMs       = millis();
    enrolledId        = -1;
    step0FailCount    = 0;
    step0StableCount  = 0;
}

// -----------------------------------------------------------------------------
// Xu ly lenh nhan tu STM32
// -----------------------------------------------------------------------------
static void process_cmd(const String &cmd)
{
    Serial.printf("[CMD] «%s»\n", cmd.c_str());

    if (cmd == "SECURE_HELLO") {
        linkEnableSecure();
        linkSendPlain("SECURE_READY");
        Serial.println("[UART] Secure mode enabled by host");
    }
    else if (cmd == "SECURE_READY") {
        linkEnableSecure();
    }
    else if (cmd == "REBOOT") {
        linkSend("BOOTING");
        Serial.println("[SYS] REBOOT requested by STM32");
        delay(50);
        ESP.restart();
    }
    else if (cmd == "ENROLL") {
        if (recognizer.get_enrolled_id_num() >= MAX_ENROLLED_FACES) {
            linkSend("DB_FULL");
            Serial.println("[ENROLL] DB full → DB_FULL sent");
            return;
        }
        // Restart enrollment unconditionally - handles STM32 retry correctly.
        // If we were mid-enroll, roll back the partial capture first.
        abort_enroll(appState == STATE_ENROLLING);
        appState    = STATE_ENROLLING;
        enrollStep  = 0;
        stepStartMs = millis();
        enrolledId   = -1;
        matchCount   = 0;
        noMatchCount = 0;
        lastMatchId  = -1;
        linkSendf("ENROLL_CFG:%d", ENROLL_TOTAL_STEPS);
        linkSend(ENROLL_STEPS[0]);
        Serial.printf("[ENROLL] Step 1/%d → %s\n", ENROLL_TOTAL_STEPS, ENROLL_STEPS[0]);
    }
    else if (cmd == "DEL_ALL") {
        recognizer.clear_id(true);
        appState     = STATE_IDLE;
        enrollStep   = 0;
        enrolledId   = -1;
        failureCount = 0;      // clear failure counter after admin delete
        lockoutUntilMs = 0;    // also clear any active lockout
        linkSend("DELETED");
        record_event("DELETED");
        Serial.println("[SYS] Face DB cleared");
    }
    else if (cmd == "CANCEL") {
        abort_enroll(true);
        Serial.println("[ENROLL] Cancelled by STM32");
    }
    else if (cmd == "STATUS") {
        send_status(true);
    }
    else {
        Serial.printf("[CMD] Unknown command: %s\n", cmd.c_str());
        linkSend("ERROR:UNKNOWN_CMD");
    }
}

// -----------------------------------------------------------------------------
// Xu ly mot frame camera: nhan dien hoac dang ky
// -----------------------------------------------------------------------------
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

    // Two-stage detection: stage 1 (MSR01) tim vung mat -> stage 2 (MNP01) tinh chinh keypoints
    // Keypoints chinh xac tu stage 2 giup recognize() dat similarity cao hon dang ke.
    std::list<dl::detect::result_t> candidates =
        s1.infer(rgbFrameBuf, {(int)fb->height, (int)fb->width, 3});
    esp_task_wdt_reset();

    std::list<dl::detect::result_t> faces =
        s2.infer(rgbFrameBuf, {(int)fb->height, (int)fb->width, 3}, candidates);
    esp_task_wdt_reset();   // two-stage inference co the cham - giu WDT happy

    // Debug: in moi 2s de khong spam, giup xac nhan co detect duoc khong
    static uint32_t lastDetectLogMs = 0;
    if ((millis() - lastDetectLogMs) >= 2000U) {
        lastDetectLogMs = millis();
        Serial.printf("[DETECT] faces=%d  size=%dx%d  state=%s\n",
                      (int)faces.size(), fb->width, fb->height, app_state_name());
    }

    if (faces.empty()) {
        // Khong thay khuon mat
        noMatchCount     = 0;
        step0StableCount = 0;  // reset stability gate - face must reappear cleanly
        if (appState == STATE_ENROLLING) {
            if ((millis() - stepStartMs) >= ENROLL_FACE_TIMEOUT_MS) {
                // Het gio cho, nhac lai buoc hien tai
                stepStartMs = millis();
                linkSend(ENROLL_STEPS[enrollStep]);
                Serial.printf("[ENROLL] Timeout, re-prompt step %d\n", enrollStep + 1);
            }
        }
        esp_camera_fb_return(fb);
        return;
    }

    auto bestFaceIt = faces.begin();
    int bestArea = -1;
    for (auto it = faces.begin(); it != faces.end(); ++it) {
        int w = (int)it->box[2] - (int)it->box[0] + 1;
        int h = (int)it->box[3] - (int)it->box[1] + 1;
        int area = w * h;
        if (area > bestArea) {
            bestArea = area;
            bestFaceIt = it;
        }
    }

    face_light_touch();

    dl::detect::result_t &face = *bestFaceIt;

    // Boc frame buffer thanh Tensor (khong copy, tro thang vao fb->buf)
    Tensor<uint8_t> img;
    img.set_element(rgbFrameBuf)
       .set_shape({(int)fb->height, (int)fb->width, 3})
       .set_auto_free(false);

    // -- Che do DANG KY -------------------------------------------------------
    if (appState == STATE_ENROLLING)
    {
        if (enrollStep == 0) {
            int result = recognizer.enroll_id(img, face.keypoint, "", true);
            if (result < 0) {
                // Alignment failed - reset stability and retry next detection
                step0FailCount++;
                step0StableCount = 0;
                Serial.printf("[ENROLL] enroll_id failed (attempt %d)\n", step0FailCount);
                esp_camera_fb_return(fb);
                return;
            }
            step0FailCount   = 0;
            step0StableCount = 0;
            enrolledId = result;
            Serial.printf("[ENROLL] Captured FRONT, ID=%d\n", result);

            // Neu chi co 1 buoc (ENROLL_TOTAL_STEPS==1), hoan tat enrollment ngay
            if (ENROLL_TOTAL_STEPS == 1) {
                int persisted = recognizer.write_ids_to_flash();
                if (persisted < 0) {
                    Serial.println("[ENROLL] WARNING: flash write failed — embedding in RAM only");
                }
                Serial.printf("[ENROLL] Done! ID=%d  total=%d  flash=%d\n",
                              enrolledId, recognizer.get_enrolled_id_num(), persisted);
                linkSendf("ENROLLED:%d", enrolledId);
                record_event("ENROLLED", enrolledId);
                appState     = STATE_IDLE;
                enrollStep   = 0;
                enrolledId   = -1;
                matchCount   = 0;
                noMatchCount = 0;
                lastMatchId  = -1;
                esp_camera_fb_return(fb);
                return;
            }

            enrollStep++;
            stepStartMs = millis();
            linkSend(ENROLL_STEPS[enrollStep]);
            Serial.printf("[ENROLL] Step 2/%d → %s\n", ENROLL_TOTAL_STEPS, ENROLL_STEPS[enrollStep]);
        }
        else {
            // Buoc 1-4: cho du ENROLL_STEP_DELAY_MS de nguoi dung kip xoay mat
            if ((millis() - stepStartMs) < ENROLL_STEP_DELAY_MS) {
                esp_camera_fb_return(fb);
                return;
            }

            bool is_last = (enrollStep == ENROLL_TOTAL_STEPS - 1);

            if (is_last) {
                int persisted = recognizer.write_ids_to_flash();
                if (persisted < 0) {
                    Serial.println("[ENROLL] WARNING: flash write failed — embedding in RAM only");
                }
                Serial.printf("[ENROLL] Done! ID=%d  total=%d  flash=%d\n",
                              enrolledId, recognizer.get_enrolled_id_num(), persisted);
                linkSendf("ENROLLED:%d", enrolledId);
                record_event("ENROLLED", enrolledId);
                appState     = STATE_IDLE;
                enrollStep   = 0;
                enrolledId   = -1;
                matchCount   = 0;
                noMatchCount = 0;
                lastMatchId  = -1;
            }
            else {
                enrollStep++;
                stepStartMs = millis();
                linkSend(ENROLL_STEPS[enrollStep]);
                Serial.printf("[ENROLL] Step %d/%d → %s\n",
                              enrollStep + 1, ENROLL_TOTAL_STEPS, ENROLL_STEPS[enrollStep]);
            }
        }
    }
    // -- Che do NHAN DIEN (IDLE) -----------------------------------------------
    else
    {
        // Bo qua neu khong co mat nao dang ky
        if (recognizer.get_enrolled_id_num() == 0) {
            noMatchCount = 0;
            esp_camera_fb_return(fb);
            return;
        }

        uint32_t now = millis();

        // -- Lockout check -----------------------------------------------------
        if (now < lockoutUntilMs) {
            noMatchCount = 0;
            esp_camera_fb_return(fb);
            return;
        }

        // -- Cooldown: khi dang trong open cooldown, reset vote va skip --------
        if ((now - lastOpenMs) < OPEN_COOLDOWN_MS) {
            matchCount  = 0;
            lastMatchId = -1;
            noMatchCount = 0;
            esp_camera_fb_return(fb);
            return;
        }

        // Skip neu denied cooldown con hieu luc
        if ((now - lastDeniedMs) < DENIED_COOLDOWN_MS) {
            noMatchCount = 0;
            esp_camera_fb_return(fb);
            return;
        }

        // -- Nhan dien ---------------------------------------------------------
        face_info_t res = recognizer.recognize(img, face.keypoint);

        if (res.id >= 0 && res.similarity >= RECOGNITION_THRESHOLD) {
            noMatchCount = 0;
            // -- VOTING: tich luy frame khop lien tiep -------------------------
            if (res.id == lastMatchId) {
                matchCount++;
            } else {
                matchCount  = 1;
                lastMatchId = res.id;
            }
            Serial.printf("[RECOG] Vote %d/%d  id=%d  sim=%.3f\n",
                          matchCount, REQUIRED_MATCHES, res.id, res.similarity);

            if (matchCount >= REQUIRED_MATCHES) {
                // Xac nhan - mo cua
                failureCount = 0;      // reset failure counter on success
                matchCount   = 0;
                lastMatchId  = -1;
                lastOpenMs   = now;
                linkSendf("OPEN:%d", res.id);
                record_event("OPEN", res.id, res.similarity);
                Serial.printf("[RECOG] CONFIRMED  id=%d  sim=%.3f\n",
                              res.id, res.similarity);
            }
        }
        else {
            // Khong khop - reset vote
            matchCount  = 0;
            lastMatchId = -1;
            noMatchCount++;
            Serial.printf("[RECOG] NO MATCH  vote=%d/%d  id=%d  sim=%.3f\n",
                          noMatchCount, REQUIRED_NO_MATCHES, res.id, res.similarity);

            if (noMatchCount < REQUIRED_NO_MATCHES) {
                esp_camera_fb_return(fb);
                return;
            }

            noMatchCount = 0;
            lastDeniedMs = now;
            failureCount++;
            Serial.printf("[RECOG] DENIED  failures=%d/%d\n",
                          failureCount, MAX_FAILURES);

            // -- Lockout trigger -----------------------------------------------
            if (failureCount >= MAX_FAILURES) {
                failureCount   = 0;
                lockoutUntilMs = now + LOCKOUT_DURATION_MS;
                linkSendf("LOCKOUT:%lu", LOCKOUT_DURATION_MS);
                record_event("LOCKOUT", -1, res.similarity);
                Serial.printf("[SECURITY] LOCKOUT for %lu ms\n", LOCKOUT_DURATION_MS);

                // Gui LOCKOUT_CLEAR sau khi het thoi gian (dung mot lan trigger)
                // - duoc xu ly boi lockout_check() trong loop()
            } else {
                linkSend("DENIED");
                record_event("DENIED", -1, res.similarity);
            }
        }
    }

    esp_camera_fb_return(fb);
}

// -----------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    esp_reset_reason_t rr = esp_reset_reason();
    Serial.printf("\n[SYS] STM-FaceGuard ESP32-S3 starting... reset=%s\n",
                  reset_reason_name(rr));
    if (rr == ESP_RST_BROWNOUT || rr == ESP_RST_PANIC ||
        rr == ESP_RST_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_INT_WDT) {
        Serial.println("[SYS] Warning: previous reset indicates power instability or runtime crash");
    }
    face_light_init();
    face_light_self_test();

    // UART toi STM32
    Serial1.begin(STM32_BAUD, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
    delay(10);
    while (Serial1.available()) Serial1.read();  // flush garbage bytes tu STM32 luc ESP32 power-up
    rxBuf.reserve(RX_BUF_MAX_LEN);
    linkSecureActive = false;
    linkTxSeq = 1U;
    linkRxSeq = 0U;
    linkRxSynced = false;
    linkSendPlain("BOOTING");
    delay(50);  // dam bao STM32 nhan duoc BOOTING truoc khi camera chiem CPU

    // Khoi dong WiFi truoc camera - AP luon hien ngay ca khi camera crash
#if PREVIEW_ENABLE
    preview_start_network();
#endif

    // Khoi tao camera
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
    // Hien thi so khuon mat da dang ky (nap tu NVS flash)
    Serial.printf("[SYS] Enrolled faces: %d\n", recognizer.get_enrolled_id_num());

    // Bat task watchdog - reset ESP32 neu vong loop() bi ket qua 30 giay
    esp_task_wdt_init(30, true);   // 30 s timeout, panic + reset
    esp_task_wdt_add(NULL);        // theo doi task hien tai (loopTask)

    delay(300);
    if (cameraReady) {
        send_status(true);
    }
}

void loop()
{
    esp_task_wdt_reset();   // reset WDT moi vong loop de chung minh khong bi ket

    // -- Kiem tra het lockout --------------------------------------------------
    if (lockoutUntilMs > 0 && millis() >= lockoutUntilMs) {
        lockoutUntilMs = 0;
        failureCount   = 0;
        matchCount     = 0;
        lastMatchId    = -1;
        linkSend("LOCKOUT_CLEAR");
        Serial.println("[SECURITY] Lockout cleared");
    }

    // -- Doc UART tu STM32 ----------------------------------------------------
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n') {
            char decoded[LINK_CRYPT_MAX_LEN + 1U];
            rxBuf.trim();
            if (rxBuf.length() > 0 && linkDecodeLine(rxBuf, decoded, sizeof(decoded))) {
                process_cmd(String(decoded));
            }
            rxBuf = "";
        } else if (rxBuf.length() >= RX_BUF_MAX_LEN) {
            /* Command too long - discard and log */
            Serial.printf("[CMD] RxBuf overflow, discarding: %s\n", rxBuf.c_str());
            rxBuf = "";
        } else if (c != '\r') {
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
            linkSendf("CAM_FAIL:%d", (int)cameraInitErr);
        }
        delay(10);
        return;
    }

    if (appState == STATE_IDLE &&
        (millis() - lastBeaconTxMs) >= AUTO_STATUS_BEACON_MS) {
        lastBeaconTxMs = millis();
        send_status(false);
    }

    /* Periodic health telemetry every 60 s - helps diagnose field issues */
    static uint32_t lastTelemetryMs = 0;
    if ((millis() - lastTelemetryMs) >= 60000UL) {
        lastTelemetryMs = millis();
        Serial.printf("[HEALTH] heap_free=%u  psram_free=%u  uptime=%lus  "
                      "faces=%d  failures=%d  wdt=ok\n",
                      esp_get_free_heap_size(),
                      heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                      millis() / 1000UL,
                      recognizer.get_enrolled_id_num(),
                      failureCount);
    }

    // -- Xu ly mot frame camera -----------------------------------------------
    process_frame();
}
