# STM-FaceGuard

**Hệ thống Khóa cửa thông minh nhận diện khuôn mặt — Dual-MCU**

Dự án đồ án môn học xây dựng hệ thống khóa cửa thông minh sử dụng kiến trúc Dual-MCU: **STM32F303RE** đảm nhận vai trò điều khiển trung tâm, **ESP32-S3 N16R8 CAM** (tích hợp camera OV3660 3MP) chạy thuật toán AI nhận diện khuôn mặt thời gian thực. Hệ thống hỗ trợ phản hồi âm thanh tiếng Việt qua DFPlayer Mini và hiển thị trạng thái trên màn hình OLED SSD1306.

---

## Mục lục

- [Tính năng](#tính-năng)
- [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
- [Danh sách linh kiện](#danh-sách-linh-kiện)
- [Hướng dẫn kết nối chi tiết](#hướng-dẫn-kết-nối-chi-tiết)
- [Cấu hình giao tiếp](#cấu-hình-giao-tiếp)
- [Hướng dẫn sử dụng](#hướng-dẫn-sử-dụng)
- [Giao thức UART](#giao-thức-uart)
- [Chuẩn bị file âm thanh](#chuẩn-bị-file-âm-thanh-dfplayer)
- [Công cụ phát triển](#công-cụ-phát-triển)
- [Cấu trúc dự án](#cấu-trúc-dự-án)

---

## Tính năng

- **Nhận diện khuôn mặt thời gian thực** — ESP32-S3 xử lý AI với camera OV3660 3MP
- **Quản lý người dùng bằng nút nhấn** — Thêm và xóa khuôn mặt offline, không cần máy tính
- **Phản hồi âm thanh tiếng Việt** — DFPlayer Mini phát 10 file MP3 hướng dẫn
- **Hiển thị trạng thái OLED** — Màn hình 128×64 px hiển thị thông tin và hướng dẫn từng bước
- **Mở cửa từ bên trong** — Nút EXIT (PC13) ưu tiên cao nhất, hoạt động ngay lập tức
- **Lưu khuôn mặt vào Flash** — Tối đa 7 khuôn mặt lưu trong NVS Flash của ESP32-S3

---

## Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────────┐
│                        TẦNG ĐẦU VÀO                             │
│  [Camera OV3660 3MP]                 [BTN_EXIT   PC13 / Nucleo] │
│       tích hợp trong                [BTN_ENROLL  PA0          ] │
│  [ESP32-S3 N16R8 CAM]               [BTN_DELETE  PA1          ] │
│         │                                                        │
└─────────┼────────────────────────────────────────────────────────┘
          │ UART1 115200 baud
          │ PA9 (TX) ←→ GPIO2 (RX)
          │ PA10(RX) ←→ GPIO1 (TX)
          ▼
┌─────────────────────────────────────────────────────────────────┐
│                   TẦNG ĐIỀU KHIỂN TRUNG TÂM                     │
│                     STM32F303RE (72 MHz)                         │
│                     Nucleo-F303RE Board                          │
└────────┬─────────────────────┬──────────────────────┬───────────┘
         │ GPIO PB0            │ USART3 9600 baud      │ I2C1 Fast
         ▼                     ▼                       ▼
┌──────────────────┐  ┌─────────────────────┐  ┌────────────────┐
│ BC547 → LY2N     │  │   DFPlayer Mini     │  │  OLED SSD1306  │
│ 12VDC Relay      │  │   + Loa 3070 3W     │  │  128×64 px     │
│      │           │  └─────────────────────┘  └────────────────┘
│ SM1373 12V Lock  │
└──────────────────┘
```

**Nguyên lý hoạt động:**
1. ESP32-S3 liên tục thu ảnh từ camera OV3660 tích hợp và chạy thuật toán Face Recognition
2. Khi nhận diện đúng → gửi `OPEN:<ID>` qua UART → STM32 kích relay mở khóa 3 giây
3. Khi thất bại → gửi `DENIED` → STM32 phát cảnh báo và hiển thị "Access Denied"

---

## Danh sách linh kiện

### I. Khối Điều khiển & AI

| STT | Tên linh kiện | Thông số | Vai trò |
|-----|--------------|----------|---------|
| 1 | **Nucleo-F303RE** | STM32F303RE, Cortex-M4, 72 MHz, 512 KB Flash | Bộ não trung tâm: state machine, relay, OLED, DFPlayer |
| 2 | **ESP32-S3 N16R8 CAM** | Dual-core 240 MHz, 16 MB Flash, 8 MB PSRAM OPI | AI node: nhận diện khuôn mặt, giao tiếp UART STM32 |
| 3 | **Camera OV3660** | 3 MP, tích hợp sẵn trên board ESP32-S3 N16R8 CAM | Thu ảnh khuôn mặt chất lượng cao |

### II. Nút nhấn

| STT | Nút | Chân STM32 | Chức năng |
|-----|-----|-----------|-----------|
| 4 | **Nút EXIT** | PC13 (Blue Button Nucleo) | Mở cửa ngay từ bên trong — ưu tiên tuyệt đối |
| 5 | **Nút ENROLL** | PA0 (EXTI0) | Kích hoạt chế độ đăng ký khuôn mặt mới |
| 6 | **Nút DELETE** | PA1 (EXTI1) | Giữ 3 giây để xóa toàn bộ khuôn mặt |

> PC13 tận dụng nút xanh (Blue Button) có sẵn trên Nucleo-F303RE.

### III. Phản hồi & Hiển thị

| STT | Linh kiện | Thông số | Vai trò |
|-----|----------|----------|---------|
| 7 | **OLED SSD1306** | 0.96", 128×64 px, I2C | Hiển thị trạng thái, hướng dẫn đăng ký |
| 8 | **DFPlayer Mini** | Giải mã MP3, UART 9600 baud | Phát âm thanh tiếng Việt |
| 9 | **Loa 3070** | 8 Ω, 3 W | Phát âm thanh |
| 10 | **Thẻ MicroSD** | 1–16 GB, FAT32 | Lưu 10 file MP3 cho DFPlayer |

### IV. Chấp hành & Nguồn

| STT | Linh kiện | Thông số | Vai trò |
|-----|----------|----------|---------|
| 11 | **Khóa SM1373 V3** | 12 V DC, Fail-Secure, ~0.5 A | Cơ cấu chốt khóa cửa |
| 12 | **Relay LY2N 12VDC** | Cuộn hút 12 V, tiếp điểm 10 A | Đóng/ngắt 12 V cho SM1373 |
| 13 | **Đế relay PTF08A** | 8 chân, socket kiểu Finder | Gắn relay LY2N dễ tháo lắp |
| 14 | **Transistor BC547** | NPN, 100 mA, 45 V | Kích relay LY2N từ GPIO 3.3 V của STM32 |
| 15 | **Nguồn 3.3 V** | Ổn định, ≥ 500 mA | Nuôi STM32 (Nucleo 3.3V pin) + OLED |
| 16 | **Nguồn 5 V** | Ổn định, ≥ 1 A | Nuôi ESP32-S3 + DFPlayer Mini |
| 17 | **Nguồn 12 V** | Ổn định, ≥ 1 A | Nuôi cuộn relay LY2N + khóa SM1373 |

### V. Phụ kiện

| STT | Linh kiện | Vai trò |
|-----|----------|---------|
| 18 | **Điện trở 1 kΩ** | Hạn dòng Base của BC547 |
| 19 | **Diode 1N4007** (×2) | Chống dòng ngược (1 cái cho relay, 1 cái cho SM1373) |
| 20 | **Dây Jumper** | Kết nối tín hiệu giữa các module |
| 21 | **Breadboard** | Mạch thử nghiệm |

---

## Hướng dẫn kết nối chi tiết

> **Quy ước:** `→` là nguồn tín hiệu/điện, `←` là đích nhận.

---

### 1. Sơ đồ nguồn điện tổng thể

Hệ thống sử dụng **3 mức điện áp riêng biệt** từ nguồn cấp sẵn:

```
Nguồn 3.3V ──► OLED VCC
           ──► Nucleo 3.3V pin (CN6 pin 4)   ← để Nucleo không bị sụt áp nội bộ

Nguồn 5V   ──► ESP32-S3 5V pin
           ──► DFPlayer Mini VCC

Nguồn 12V  ──► LY2N Coil A1 (+)  [qua BC547, xem mục 5]
           ──► LY2N COM           [tiếp điểm cấp 12V cho SM1373]
```

#### Điểm nối đất chung (GND) — bắt buộc

Tất cả các dây GND sau đây phải nối về **cùng một điểm**:

| Thiết bị | Chân GND cần nối |
|----------|-----------------|
| Nguồn 3.3V | GND (–) |
| Nguồn 5V | GND (–) |
| Nguồn 12V | GND (–) |
| Nucleo-F303RE | CN6 pin 6 (GND) |
| ESP32-S3 | GND pin |
| OLED SSD1306 | GND pin |
| DFPlayer Mini | GND pin |
| BC547 | Emitter (chân E) |
| SM1373 | Dây ĐEN (–) |

> **Lý do:** UART giữa STM32 và ESP32-S3 cần GND chung làm điện áp tham chiếu. Nếu thiếu → tín hiệu UART nhiễu hoặc mất hoàn toàn → hệ thống không hoạt động.

---

### 2. ESP32-S3 N16R8 CAM ↔ STM32F303RE (UART1)

Đây là kết nối quan trọng nhất — chú ý **TX nối RX, RX nối TX**.

```
ESP32-S3          STM32F303RE (Nucleo CN10)     Nguồn
─────────         ──────────────────────────    ──────
5V           ◄──  (không nối STM32)         ◄── Nguồn 5V
GND          ───  GND                       ─── GND chung
GPIO1  (TX)  ──►  PA10 / D2  (USART1 RX)
GPIO2  (RX)  ◄──  PA9  / D8  (USART1 TX)
```

> Camera OV3660 tích hợp sẵn trên board ESP32-S3, **không cần nối thêm dây nào** cho camera.

**Lưu ý điện áp:** ESP32-S3 và STM32F303RE đều giao tiếp mức 3.3V — kết nối trực tiếp tín hiệu UART, không cần level shifter.

---

### 3. OLED SSD1306 ↔ STM32F303RE (I2C1)

![OLED SSD1306 Pinout](docs/oled-ssd1306_pinout.png)

```
OLED SSD1306      STM32F303RE (Nucleo CN10)     Nguồn ngoài
────────────      ──────────────────────────    ────────────
VCC          ◄──────────────────────────────── Nguồn 3.3V
GND          ──────────────────────────────── GND chung
SCL          ──►  PB8 / D15  (I2C1 SCL)
SDA          ──►  PB9 / D14  (I2C1 SDA)
```

> Địa chỉ I2C mặc định của module: **0x3C**. Không cần điện trở pull-up thêm nếu module đã tích hợp sẵn.

---

### 4. DFPlayer Mini ↔ STM32F303RE (USART3)

![DFPlayer Mini Pinout](docs/dfplayer-mini_pinout.png)

```
DFPlayer Mini     STM32F303RE (Nucleo CN7)     Nguồn
─────────────     ──────────────────────────   ──────
VCC          ◄──  (không nối STM32)        ◄── Nguồn 5V
GND          ───  GND                      ─── GND chung
RX           ◄──  [1 kΩ] ── PC10  (USART3 TX)  ← bắt buộc có điện trở 1kΩ
TX           ──►  PC11  (USART3 RX)
SPK+         ──►  Loa 3070 chân (+)
SPK–         ──►  Loa 3070 chân (–)
```

> **Bắt buộc** đặt điện trở **1 kΩ** nối tiếp trên dây PC10 → DFPlayer RX để bảo vệ chip DFPlayer khỏi mức điện áp cao.

---

### 5. Mạch kích Relay LY2N 12VDC (BC547 + STM32 PB0)

Relay LY2N có cuộn hút **12V / ~40mA** — không thể kích trực tiếp từ GPIO 3.3V. Cần transistor BC547 làm công tắc.

#### Sơ đồ mạch BC547:

```
STM32 PB0 (3.3V)
      │
    [1kΩ]
      │
      ├──► BC547 Base  (chân B)
             │
      BC547 Collector (chân C) ──► LY2N A2 (cuộn âm, chân 12 đế PTF08A)
      BC547 Emitter   (chân E) ──► GND chung

Nguồn 12V ────────────────────► LY2N A1 (cuộn dương, chân 11 đế PTF08A)

1N4007 (flyback): Anode → A2, Cathode → A1  [bảo vệ BC547 khỏi xung cảm ứng]
```

#### Sơ đồ chân BC547 nhìn từ mặt phẳng (chữ nổi hướng ra ngoài):

```
  [B]  [C]  [E]
  Base Collector Emitter
```

#### Nguyên lý:
- PB0 = **HIGH (3.3V)** → BC547 dẫn → Relay LY2N có điện (12V) → Tiếp điểm NO đóng → SM1373 mở khóa
- PB0 = **LOW (0V)**  → BC547 tắt → Relay nhả → Tiếp điểm NO mở → SM1373 khóa

---

### 6. Relay LY2N + Đế PTF08A ↔ Khóa SM1373

#### Sơ đồ chân PTF08A (nhìn từ phía cắm dây):

```
   ┌─────────────────────────────┐
   │  11  12  14  │  21  22  24  │   ← hàng tiếp điểm
   │                             │
   │       1    2                │   ← chân cuộn dây A1, A2
   └─────────────────────────────┘
   (Số chân theo chuẩn Omron/Finder — kiểm tra nhãn trên đế thực tế)
```

| Chân PTF08A | Nối tới | Ghi chú |
|------------|---------|---------|
| A1 (cuộn +) | 12V | Nguồn cuộn hút |
| A2 (cuộn –) | BC547 Collector | Qua BC547 xuống GND |
| COM (chân 12) | 12V | Cấp nguồn qua tiếp điểm |
| NO  (chân 11) | SM1373 dây ĐỎ (+) | Normally Open — đóng khi relay có điện |

```
Kết nối SM1373:
Nguồn 12V ──► LY2N COM (chân 12 PTF08A)
               LY2N NO  (chân 11 PTF08A) ──► SM1373 dây ĐỎ  (+)
GND chung ──────────────────────────────── SM1373 dây ĐEN (–)

1N4007 (bảo vệ SM1373): Anode → dây ĐEN, Cathode → dây ĐỎ
(mắc song song với SM1373, chống xung ngược khi ngắt điện)
```

> **SM1373 V3 — Fail-Secure:** Mất điện = khóa cứng. Có điện = mở chốt.

---

### 7. Nút nhấn ↔ STM32F303RE

Cả 3 nút dùng kiểu **active-LOW**: nhấn nút → chân GPIO nối GND → phát EXTI.

```
3.3V (pull-up nội bộ trong STM32)
  │
  ├── PA0  (BTN_ENROLL) ──[Nút ENROLL]── GND
  ├── PA1  (BTN_DELETE) ──[Nút DELETE]── GND
  └── PC13 (BTN_EXIT)   ──[Blue Button Nucleo]── GND  (đã có sẵn trên board)
```

> Code đã cấu hình PULLUP + FALLING edge cho PA0, PA1, PC13 trong `App_Init()`.

---

### 8. Tổng hợp pinout STM32F303RE

![Nucleo-F303RE Pinout](docs/nucleo-f303_pinout.png)

| Chân STM32 | Connector Nucleo | Tín hiệu | Kết nối tới |
|-----------|-----------------|----------|-------------|
| **PA9** | CN10 – D8 | USART1 TX | ESP32-S3 **GPIO2** (RX) |
| **PA10** | CN10 – D2 | USART1 RX | ESP32-S3 **GPIO1** (TX) |
| **PC10** | CN7 – D45 | USART3 TX | DFPlayer **RX** (qua 1kΩ) |
| **PC11** | CN7 – D44 | USART3 RX | DFPlayer **TX** |
| **PA2** | CN10 – D1 | USART2 TX | ST-Link Virtual COM (debug) |
| **PA3** | CN10 – D0 | USART2 RX | ST-Link Virtual COM (debug) |
| **PB8** | CN10 – D15 | I2C1 SCL | OLED **SCL** |
| **PB9** | CN10 – D14 | I2C1 SDA | OLED **SDA** |
| **PB0** | CN10 – D3 | GPIO OUT | [1kΩ] → **BC547 Base** |
| **PA5** | CN10 – D13 | GPIO OUT | LED xanh trên Nucleo (LD2) |
| **PC13** | — | EXTI13 | Blue Button Nucleo (BTN_EXIT) |
| **PA0** | CN10 – D2\* | EXTI0 | Nút **ENROLL** → GND |
| **PA1** | CN10 – D3\* | EXTI1 | Nút **DELETE** → GND |

---

### 9. Tổng hợp pinout ESP32-S3 N16R8 CAM

![ESP32-S3 N16R8 CAM Pinout](docs/esp32s3-cam_pinout.png)

| GPIO | Chức năng | Kết nối |
|------|----------|---------|
| **GPIO1** | UART1 TX | → STM32 PA10 (USART1 RX) |
| **GPIO2** | UART1 RX | ← STM32 PA9 (USART1 TX) |
| GPIO4 | Camera SIOD | Tích hợp sẵn trên board |
| GPIO5 | Camera SIOC | Tích hợp sẵn trên board |
| GPIO6 | Camera VSYNC | Tích hợp sẵn trên board |
| GPIO7 | Camera HREF | Tích hợp sẵn trên board |
| GPIO8–13 | Camera D2–D7 | Tích hợp sẵn trên board |
| GPIO15 | Camera XCLK | Tích hợp sẵn trên board |
| GPIO16–18 | Camera D7–D5 | Tích hợp sẵn trên board |
| **5V** | Nguồn | LM2596 OUT+ |
| **GND** | Đất | GND chung |

---

## Cấu hình giao tiếp

### USART1 — ESP32-S3 ↔ STM32 (115200 baud)

| Thông số | Giá trị |
|---------|--------|
| Baud rate | 115200 |
| Data / Stop / Parity | 8N1 |
| STM32 TX | PA9 |
| STM32 RX | PA10 |
| ESP32-S3 TX | GPIO1 |
| ESP32-S3 RX | GPIO2 |

### USART3 — DFPlayer Mini ↔ STM32 (9600 baud)

| Thông số | Giá trị |
|---------|--------|
| Baud rate | 9600 |
| Data / Stop / Parity | 8N1 |
| STM32 TX | PC10 (qua 1kΩ → DFPlayer RX) |
| STM32 RX | PC11 ← DFPlayer TX |

### I2C1 — OLED SSD1306 ↔ STM32

| Thông số | Giá trị |
|---------|--------|
| Chế độ | Fast Mode (400 kHz) |
| SCL | PB8 |
| SDA | PB9 |
| Địa chỉ OLED | 0x3C |

---

## Hướng dẫn sử dụng

### Nhận diện tự động (IDLE)

ESP32-S3 liên tục quét khuôn mặt:
- **Khớp** → Relay mở 3 giây, OLED hiện `"Welcome #ID"`, loa phát *"Xin chào, cửa đã mở"*
- **Không khớp** → OLED hiện `"Access Denied"`, loa phát cảnh báo
- **Không có mặt đăng ký** → hệ thống không phát cảnh báo, chờ im lặng

### Đăng ký khuôn mặt mới (ENROLL)

1. Đứng trước camera, khoảng cách **30–60 cm**, ánh sáng đủ sáng
2. Nhấn nút **ENROLL** (PA0)
3. OLED hiện `"Enrolling..."`, loa phát *"Mời bạn nhìn vào camera"*
4. ESP32-S3 hướng dẫn 5 tư thế — **giữ mỗi tư thế ~2.5 giây**:

| Bước | OLED | Loa | Hành động |
|------|------|-----|-----------|
| 1/5 | `Step 1/5 STRAIGHT` | Track 6 | Nhìn thẳng — **ảnh được chụp tại bước này** |
| 2/5 | `Step 2/5 LEFT` | Track 7 | Quay đầu sang trái ~30°, giữ 2.5s |
| 3/5 | `Step 3/5 RIGHT` | Track 8 | Quay đầu sang phải ~30°, giữ 2.5s |
| 4/5 | `Step 4/5 UP` | Track 9 | Ngước đầu lên ~20°, giữ 2.5s |
| 5/5 | `Step 5/5 DOWN` | Track 10 | Cúi đầu xuống ~20°, giữ 2.5s |

5. Hoàn tất: OLED hiện `"Enrolled #X"`, loa phát *"Đã thêm khuôn mặt thành công"*

> **Lưu ý:** Nếu không phát hiện khuôn mặt trong 10 giây, OLED nhắc lại bước hiện tại. Sau 15 giây không có phản hồi từ ESP32, STM32 tự động hủy đăng ký.

### Xóa toàn bộ khuôn mặt (DELETE)

1. Nhấn và **giữ** nút DELETE (PA1) trong **3 giây**
2. OLED hiện đếm ngược `"Hold 1s... 2s... 3s"`
3. Xác nhận: OLED hiện `"Deleting..."`, ESP32-S3 xóa toàn bộ NVS flash
4. Loa phát *"Đã xóa toàn bộ dữ liệu khuôn mặt"*
5. Thả sớm trước 3s → hủy, không xóa

### Mở cửa từ bên trong (EXIT)

1. Nhấn nút **EXIT** (PC13 — Blue Button Nucleo)
2. STM32 kích relay **ngay lập tức**, không qua ESP32
3. SM1373 mở chốt trong **3 giây**, OLED hiện `"Exit Unlocked"`

---

## Giao thức UART

### ESP32-S3 → STM32

| Lệnh | Khi nào | Ý nghĩa |
|------|---------|---------|
| `READY\n` | Khởi động xong | ESP32 sẵn sàng |
| `OPEN:<ID>\n` | Nhận diện khớp | Mở cửa, ID = số khuôn mặt |
| `DENIED\n` | Không nhận diện được | Từ chối |
| `ENROLLED:<ID>\n` | Đăng ký xong | Khuôn mặt mới, ID mới |
| `DELETED\n` | Xóa xong | Toàn bộ DB đã xóa |
| `ENROLL_FRONT\n` | Bước 1/5 | Hướng dẫn nhìn thẳng |
| `ENROLL_LEFT\n` | Bước 2/5 | Hướng dẫn quay trái |
| `ENROLL_RIGHT\n` | Bước 3/5 | Hướng dẫn quay phải |
| `ENROLL_UP\n` | Bước 4/5 | Hướng dẫn ngẩng lên |
| `ENROLL_DOWN\n` | Bước 5/5 | Hướng dẫn cúi xuống |

### STM32 → ESP32-S3

| Lệnh | Khi nào | Ý nghĩa |
|------|---------|---------|
| `ENROLL\n` | Nhấn BTN_ENROLL | Bắt đầu đăng ký khuôn mặt |
| `DEL_ALL\n` | Giữ BTN_DELETE 3s | Xóa toàn bộ database |
| `CANCEL\n` | Timeout 15s lúc đăng ký | Hủy đăng ký |

### STM32 → DFPlayer Mini (USART3, 9600 baud)

Giao thức binary 10 byte:

```
0x7E  0xFF  0x06  CMD  0x00  ParamH  ParamL  CkH  CkL  0xEF
```

Checksum = `-(0xFF + 0x06 + CMD + 0x00 + ParamH + ParamL)`

---

## Chuẩn bị file âm thanh (DFPlayer)

### Nội dung 10 file MP3

| Track | File | Khi nào phát | Nội dung tiếng Việt |
|-------|------|-------------|---------------------|
| 1 | `0001.mp3` | Mở cửa thành công | *"Xin chào, cửa đã mở"* |
| 2 | `0002.mp3` | Nhận diện thất bại | *"Không nhận diện được, vui lòng thử lại"* |
| 3 | `0003.mp3` | Bắt đầu đăng ký | *"Mời bạn nhìn vào camera"* |
| 4 | `0004.mp3` | Đăng ký thành công | *"Đã thêm khuôn mặt thành công"* |
| 5 | `0005.mp3` | Xóa xong | *"Đã xóa toàn bộ dữ liệu khuôn mặt"* |
| 6 | `0006.mp3` | Bước 1/5 FRONT | *"Mời nhìn thẳng vào camera"* |
| 7 | `0007.mp3` | Bước 2/5 LEFT | *"Vui lòng quay đầu sang trái"* |
| 8 | `0008.mp3` | Bước 3/5 RIGHT | *"Vui lòng quay đầu sang phải"* |
| 9 | `0009.mp3` | Bước 4/5 UP | *"Vui lòng ngước đầu lên một chút"* |
| 10 | `0010.mp3` | Bước 5/5 DOWN | *"Vui lòng cúi đầu xuống một chút"* |

### Cách tạo file MP3

**VBee Studio** (giọng Việt tự nhiên nhất): [vbee.vn](https://vbee.vn) → Studio → nhập text → chọn giọng → tải MP3

**FreeText2Speech**: [freetts.com](https://freetts.com) → chọn Vietnamese → Export MP3

### Cấu trúc thẻ MicroSD (FAT32)

```
[Thư mục gốc thẻ nhớ — không tạo thư mục con]
├── 0001.mp3
├── 0002.mp3
├── 0003.mp3
├── 0004.mp3
├── 0005.mp3
├── 0006.mp3
├── 0007.mp3
├── 0008.mp3
├── 0009.mp3
└── 0010.mp3
```

> Copy file theo **đúng thứ tự 0001 → 0010**, eject thẻ đúng cách trước khi rút.

---

## Công cụ phát triển

| Công cụ | Mục đích |
|---------|---------|
| **STM32CubeIDE** | IDE lập trình + nạp code STM32F303RE |
| **STM32CubeMX 6.17** | Cấu hình Pinout, Clock, Peripheral |
| **STM32 HAL FW_F3 V1.11.6** | Thư viện HAL cho STM32F3 |
| **Arduino IDE** | Lập trình ESP32-S3 |
| **ESP32 Arduino core ≥ 2.0.6** | Board package + thư viện AI nhận diện mặt |
| **ST-Link V2-1 (tích hợp Nucleo)** | Nạp + debug STM32 |

### Cài đặt board ESP32-S3 trong Arduino IDE

```
Board:            ESP32S3 Dev Module
PSRAM:            OPI PSRAM
Flash Size:       16MB (128Mb)
Flash Mode:       QIO 80MHz
Partition Scheme: Huge APP (3MB No OTA / 1MB SPIFFS)
USB CDC On Boot:  Enabled
```

---

## Cấu trúc dự án

```
STM-FaceGuard/
├── STM32/                          # Firmware STM32F303RE (STM32CubeIDE)
│   ├── Core/
│   │   ├── Src/
│   │   │   ├── main.c              # State machine + UART/EXTI callbacks
│   │   │   ├── ssd1306.c           # OLED driver (SSD1306) + font 5×7
│   │   │   ├── dfplayer.c          # DFPlayer Mini UART protocol
│   │   │   └── stm32f3xx_it.c      # IRQ handlers (EXTI, USART)
│   │   └── Inc/
│   │       ├── main.h              # Pin definitions
│   │       ├── ssd1306.h
│   │       └── dfplayer.h          # Track number defines
│   ├── Drivers/                    # HAL + CMSIS
│   ├── STM-FaceGuard.ioc           # CubeMX config
│   └── STM32F303RETX_FLASH.ld      # Linker script
│
├── ESP32-S3/                       # Firmware ESP32-S3 (Arduino IDE)
│   └── STM-FaceGuard/
│       └── STM-FaceGuard.ino       # Face recognition + UART STM32
│
└── README.md
```

---

## Thông số hệ thống

| Thông số | Giá trị |
|---------|--------|
| Vi điều khiển chính | STM32F303RET6, Cortex-M4, 72 MHz |
| Flash / RAM STM32 | 512 KB / 64 KB |
| Vi điều khiển AI | ESP32-S3 Dual-core, 240 MHz |
| Flash / PSRAM ESP32 | 16 MB / 8 MB OPI |
| Camera | OV3660, 3 MP, tích hợp board |
| Khuôn mặt tối đa | **7 khuôn mặt** (giới hạn thư viện) |
| Thời gian nhận diện | < 1 giây |
| Thời gian mở cửa | 3 giây |
| Thời gian giữ mỗi tư thế đăng ký | 2.5 giây |
| Timeout đăng ký | 15 giây (STM32) / 10 giây/bước (ESP32) |
| Relay | LY2N 12VDC, tiếp điểm 10 A |
| Điện áp khóa SM1373 | 12 V DC |
| Nguồn cấp | Adapter 12 V – 2 A |
