# STM-FaceGuard

**Hệ thống Khóa thông minh nhận diện khuôn mặt trên STM32**

Dự án đồ án môn học xây dựng một hệ thống khóa cửa thông minh sử dụng kiến trúc Dual-MCU: **STM32F303RE** đảm nhận vai trò điều khiển trung tâm, **ESP32-S3 N16R8** chạy thuật toán AI nhận diện khuôn mặt thời gian thực qua Camera OV3660. Hệ thống hỗ trợ phản hồi âm thanh tiếng Việt qua DFPlayer Mini và hiển thị trạng thái trên màn hình OLED.

---

## Mục lục

- [Tính năng](#tính-năng)
- [Kiến trúc hệ thống](#kiến-trúc-hệ-thống)
- [Danh sách linh kiện](#danh-sách-linh-kiện)
- [Sơ đồ kết nối chân (Pinout)](#sơ-đồ-kết-nối-chân-pinout)
- [Cấu hình giao tiếp](#cấu-hình-giao-tiếp)
- [Hướng dẫn sử dụng](#hướng-dẫn-sử-dụng)
- [Giao thức UART](#giao-thức-uart)
- [Công cụ phát triển](#công-cụ-phát-triển)
- [Cấu trúc dự án](#cấu-trúc-dự-án)

---

## Tính năng

- **Nhận diện khuôn mặt thời gian thực** — ESP32-S3 xử lý AI, nhận diện trong vòng dưới 1 giây
- **Quản lý người dùng bằng nút nhấn** — Thêm và xóa khuôn mặt hoàn toàn offline, không cần máy tính
- **Phản hồi âm thanh tiếng Việt** — DFPlayer Mini phát file MP3 chào mừng hoặc cảnh báo
- **Hiển thị trạng thái OLED** — Màn hình 128×64 px hiển thị thông tin người dùng và hướng dẫn
- **Mở cửa từ bên trong** — Nút EXIT vật lý ưu tiên cao nhất, hoạt động ngay lập tức
- **Lưu trữ tối ưu 50–100 khuôn mặt** — Dữ liệu lưu trong Flash 16MB của ESP32-S3

---

## Kiến trúc hệ thống

```
┌─────────────────────────────────────────────────────────────┐
│                      TẦNG ĐẦU VÀO                           │
│   [Camera OV3660] ──► [ESP32-S3 N16R8]  [BTN_EXIT  PC13]   │
│                              │           [BTN_ENROLL PA0 ]   │
│                              │           [BTN_DELETE PA1 ]   │
└──────────────────────────────┼──────────────────────────────┘
                               │ UART1 (115200 baud)
                               ▼
┌─────────────────────────────────────────────────────────────┐
│                  TẦNG ĐIỀU KHIỂN TRUNG TÂM                  │
│                    STM32F303RE (72 MHz)                      │
│                    Nucleo-F303RE Board                       │
└──────┬──────────────────┬────────────────────┬──────────────┘
       │ GPIO (PB0)       │ UART3 (9600 baud)  │ I2C1 Fast
       ▼                  ▼                    ▼
┌─────────────┐  ┌─────────────────┐  ┌──────────────────┐
│ RELAY → 12V │  │  DFPlayer Mini  │  │   OLED 0.96"     │
│ Solenoid LY │  │    + Loa 3W     │  │   128×64 px      │
└─────────────┘  └─────────────────┘  └──────────────────┘
```

**Nguyên lý hoạt động:**
1. ESP32-S3 liên tục thu ảnh từ Camera OV3660 và chạy thuật toán Face Recognition
2. Khi nhận diện đúng khuôn mặt, gửi lệnh `OPEN:<ID>` qua UART1 → STM32
3. STM32 kích Relay mở khóa Solenoid 3 giây, phát âm thanh chào mừng, hiển thị tên người dùng lên OLED
4. Khi nhận diện thất bại, ESP32 gửi `DENIED`, STM32 phát cảnh báo và hiển thị "Từ chối"

---

## Danh sách linh kiện

### I. Khối Điều khiển & AI

| STT | Tên linh kiện | Thông số kỹ thuật | Vai trò |
|-----|--------------|-------------------|---------|
| 1 | **Nucleo-F303RE** | ARM Cortex-M4, 72 MHz, 512 KB Flash, 64 KB RAM | Bộ não trung tâm: quản lý logic, đọc nút nhấn, điều khiển Relay và hiển thị |
| 2 | **ESP32-S3 N16R8** | Dual-core Xtensa LX7, 240 MHz, 16 MB Flash, 8 MB PSRAM | Mắt thần AI: chạy thuật toán Face Recognition, giao tiếp UART với STM32 |
| 3 | **Camera OV3660** | Độ phân giải 3 MP, kết nối FPC | Thu thập hình ảnh khuôn mặt chất lượng cao cho ESP32-S3 |

### II. Khối Nút nhấn Chức năng

| STT | Tên linh kiện | Chân STM32 | Chức năng |
|-----|--------------|-----------|-----------|
| 4 | **Nút EXIT** | PC13 (EXTI13) | Mở cửa ngay lập tức từ bên trong — ưu tiên cao nhất |
| 5 | **Nút ENROLL** | PA0 (EXTI0) | Nhấn để kích hoạt chế độ học và lưu khuôn mặt mới |
| 6 | **Nút DELETE** | PA1 (EXTI1) | Nhấn giữ 3 giây để xóa toàn bộ dữ liệu khuôn mặt |

> **Lưu ý:** PC13 tận dụng nút xanh Blue Button có sẵn trên board Nucleo-F303RE để test nhanh.

### III. Khối Phản hồi & Hiển thị

| STT | Tên linh kiện | Thông số kỹ thuật | Vai trò |
|-----|--------------|-------------------|---------|
| 7 | **Màn hình OLED 0.96"** | 128×64 px, giao tiếp I2C | Hiển thị trạng thái, tên người dùng và hướng dẫn thao tác |
| 8 | **Module DFPlayer Mini** | Giải mã MP3, điều khiển qua UART | Quản lý và phát file âm thanh hướng dẫn tiếng Việt |
| 9 | **Loa chữ nhật 3070** | 8 Ω, 3 W | Phát âm thanh chào mừng hoặc cảnh báo |
| 10 | **Thẻ nhớ MicroSD** | 1 GB – 16 GB, FAT32 | Lưu trữ file âm thanh `.mp3` cho DFPlayer |

### IV. Khối Chấp hành & Nguồn

| STT | Tên linh kiện | Thông số kỹ thuật | Vai trò |
|-----|--------------|-------------------|---------|
| 11 | **Khóa Solenoid LY-01** | Điện áp 12 V DC | Cơ cấu chốt khóa vật lý cho cửa |
| 12 | **Module Relay 5 V (1 kênh)** | Opto cách ly, kích mức thấp | Đóng/ngắt nguồn 12 V cho khóa từ tín hiệu 3,3 V của STM32 |
| 13 | **Module Buck LM2596** | Hạ áp DC-DC, dòng tối đa 3 A | Hạ áp từ 12 V xuống 5 V nuôi Nucleo và ESP32-S3 |
| 14 | **Adapter 12 V – 2 A** | Nguồn xung ổn định | Cung cấp năng lượng tổng cho toàn bộ hệ thống |

### V. Phụ kiện Bảo vệ & Kết nối

| STT | Tên linh kiện | Vai trò |
|-----|--------------|---------|
| 15 | **Diode 1N4007** | Mắc song song cuộn dây Solenoid — chống dòng cảm ứng ngược bảo vệ Relay |
| 16 | **Dây cắm Jumper** | Kết nối tín hiệu giữa các module (Đực-Đực, Đực-Cái, Cái-Cái) |
| 17 | **Breadboard** | Mạch cắm thử nghiệm trước khi hàn cố định |

---

## Sơ đồ kết nối chân (Pinout)

### STM32F303RE (Nucleo-F303RE) — Toàn bộ chân sử dụng

| Chân STM32 | Nhãn trong CubeMX | Kết nối tới | Giao thức |
|-----------|------------------|------------|-----------|
| **PA9** | `ESP32_TX` | ESP32-S3 RX | USART1 TX |
| **PA10** | `ESP32_RX` | ESP32-S3 TX | USART1 RX |
| **PC10** | `DFP_TX` | DFPlayer RX | USART3 TX |
| **PC11** | `DFP_RX` | DFPlayer TX | USART3 RX |
| **PA2** | `USART_TX` | ST-Link Virtual COM | USART2 TX |
| **PA3** | `USART_RX` | ST-Link Virtual COM | USART2 RX |
| **PB8** | `OLED_SCL` | OLED SCL | I2C1 SCL |
| **PB9** | `OLED_SDA` | OLED SDA | I2C1 SDA |
| **PB0** | `RELAY` | Module Relay IN | GPIO Output |
| **PA5** | `LD2 [Green Led]` | LED xanh trên board | GPIO Output |
| **PC13** | `BTN_EXIT` | Nút EXIT (GND) | EXTI13 Pull-up |
| **PA0** | `BTN_ENROLL` | Nút ENROLL (GND) | EXTI0 Pull-up |
| **PA1** | `BTN_DELETE` | Nút DELETE (GND) | EXTI1 Pull-up |

### Kết nối nguồn điện

```
Adapter 12V ──► LM2596 ──► 5V ──► Nucleo VIN & ESP32-S3 VCC
            └──────────────────► Relay COM ──► Solenoid (+)
                                               Solenoid (–) ──► GND
                                               Diode 1N4007 song song Solenoid
```

> **Quan trọng:** Không lấy nguồn 3,3 V từ Nucleo để nuôi ESP32-S3. ESP32-S3 có thể tiêu tới 500 mA khi chạy AI — sẽ làm sụt áp và gây lỗi hệ thống.

---

## Cấu hình giao tiếp

### USART1 — ESP32-S3 ↔ STM32
| Thông số | Giá trị |
|---------|--------|
| Baud rate | 115200 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Chân TX | PA9 |
| Chân RX | PA10 |

### USART3 — DFPlayer Mini ↔ STM32
| Thông số | Giá trị |
|---------|--------|
| Baud rate | 9600 |
| Data bits | 8 |
| Stop bits | 1 |
| Parity | None |
| Chân TX | PC10 |
| Chân RX | PC11 |

### I2C1 — OLED ↔ STM32
| Thông số | Giá trị |
|---------|--------|
| Chế độ | Fast Mode |
| Timing | 0x0010020A |
| Chân SCL | PB8 |
| Chân SDA | PB9 |

---

## Hướng dẫn sử dụng

### Thêm khuôn mặt mới (ENROLL)
1. Đứng trước Camera OV3660, khoảng cách 30–60 cm
2. Nhấn nút **ENROLL** (PA0)
3. OLED hiển thị `"Scanning..."`, DFPlayer phát _"Mời bạn nhìn vào camera"_
4. ESP32-S3 chụp và lưu đặc trưng khuôn mặt vào Flash
5. OLED hiển thị `"Enrolled ID: X"`, DFPlayer phát _"Đã thêm thành công"_

### Xóa toàn bộ khuôn mặt (DELETE)
1. Nhấn và giữ nút **DELETE** (PA1) trong **3 giây**
2. OLED hiển thị `"Hold to confirm..."` → `"Deleting..."`
3. ESP32-S3 xóa toàn bộ database khuôn mặt
4. DFPlayer phát _"Đã xóa toàn bộ dữ liệu"_

### Mở cửa từ bên trong (EXIT)
1. Nhấn nút **EXIT** (PC13)
2. STM32 kích Relay ngay lập tức (không qua ESP32)
3. Solenoid mở chốt trong **3 giây**

### Nhận diện tự động
- ESP32-S3 liên tục quét khuôn mặt
- Nhận diện **thành công**: Relay mở 3 giây, OLED hiển thị `"Welcome, [Tên]!"`, DFPlayer phát tiếng chào
- Nhận diện **thất bại**: OLED hiển thị `"Access Denied"`, DFPlayer phát cảnh báo

---

## Giao thức UART

### ESP32-S3 → STM32 (Kết quả nhận diện)

| Lệnh | Ý nghĩa |
|------|---------|
| `OPEN:<ID>\n` | Nhận diện thành công, ID người dùng |
| `DENIED\n` | Nhận diện thất bại |
| `ENROLLED:<ID>\n` | Đã lưu khuôn mặt mới thành công |
| `DELETED\n` | Đã xóa toàn bộ dữ liệu |

### STM32 → ESP32-S3 (Lệnh điều khiển)

| Lệnh | Ý nghĩa |
|------|---------|
| `ENROLL\n` | Bắt đầu chế độ học khuôn mặt mới |
| `DEL_ALL\n` | Xóa toàn bộ dữ liệu khuôn mặt |

### STM32 → DFPlayer Mini (UART3, 9600 baud)
DFPlayer được điều khiển qua giao thức binary. STM32 gửi lệnh phát file MP3 theo số thứ tự tương ứng với từng sự kiện hệ thống.

---

## Công cụ phát triển

| Công cụ | Mục đích |
|---------|---------|
| **STM32CubeIDE** | IDE lập trình và nạp code cho STM32F303RE |
| **STM32CubeMX 6.17.0** | Cấu hình Pinout, Clock, Peripheral |
| **STM32 HAL Library (FW_F3 V1.11.6)** | Thư viện phần cứng trừu tượng cho STM32F3 |
| **Arduino IDE / PlatformIO** | Lập trình ESP32-S3 với thư viện ESP-WHO |
| **ESP-WHO Framework** | Thư viện AI nhận diện khuôn mặt của Espressif |
| **ST-Link V2-1 (tích hợp trên Nucleo)** | Nạp code và debug không cần mạch nạp rời |

### Cấu hình ESP32-S3 trong Arduino IDE / PlatformIO
```
Board:      ESP32S3 Dev Module
PSRAM:      OPI PSRAM
Flash Size: 16MB (128Mb)
Flash Mode: QIO 80MHz
```

---

## Cấu trúc dự án

```
STM-FaceGuard/
├── Core/
│   ├── Src/
│   │   └── main.c          # Logic điều khiển chính
│   └── Inc/
│       └── main.h
├── Drivers/
│   ├── CMSIS/              # CMSIS Core headers
│   └── STM32F3xx_HAL_Driver/  # HAL Library STM32F3
├── STM-FaceGuard.ioc       # File cấu hình STM32CubeMX
├── STM32F303RETX_FLASH.ld  # Linker script
└── README.md
```

---

## Thông số hệ thống

| Thông số | Giá trị |
|---------|--------|
| Vi điều khiển chính | STM32F303RET6 (LQFP64) |
| Tốc độ xử lý | 72 MHz (PLL từ HSE 8 MHz) |
| Bộ nhớ Flash STM32 | 512 KB |
| RAM STM32 | 64 KB |
| Vi điều khiển AI | ESP32-S3 Dual-core 240 MHz |
| Flash ESP32 | 16 MB |
| PSRAM ESP32 | 8 MB (OPI) |
| Số khuôn mặt tối ưu | 50 – 100 |
| Tốc độ nhận diện | < 1 giây |
| Điện áp hệ thống | 3,3 V (logic) / 12 V (khóa) |
| Nguồn cấp | Adapter 12 V – 2 A |
