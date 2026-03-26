# 📐 Hướng dẫn Vẽ Sơ đồ Nguyên lý trong Proteus

## 📌 Giới thiệu

**Proteus Design Suite** là phần mềm CAD chuyên dụng để vẽ sơ đồ nguyên lý (schematic) và mô phỏng mạch điện. Tài liệu này hướng dẫn cách vẽ sơ đồ nguyên lý hoàn chỉnh cho dự án **STM-FaceGuard** từ A-Z.

**Lợi ích của Proteus:**
- ✅ Thư viện linh kiện phong phú (STM32, ESP32, ICs thông thường, v.v.)
- ✅ Vẽ sơ đồ chuyên nghiệp với công cụ mạnh
- ✅ Kiểm tra lỗi kết nối (ERC - Electrical Rule Checker)
- ✅ Xuất bản vẽ PDF/PNG chất lượng cao cho luận văn
- ✅ Tích hợp PCB design (ARES module) nếu muốn nâng cấp

---

## 💾 Cài đặt Proteus

### **Tải về**

1. Truy cập [www.labcenter.com](https://www.labcenter.com/)
2. Tải phiên bản mới nhất (**Proteus 8 trở lên**, khuyến nghi v8.13+)
3. Cài đặt trên Windows/Linux (macOS không hỗ trợ chính thức, dùng VM hoặc Wine)

### **Cấp phép**

- **Trial 30 ngày**: Không cần cấp phép (đủ untuk vẽ sơ đồ)
- **Bản dử học sinh**: Liên hệ nhà trường, có thể có giảm giá
- **Bản dự án**: ~$50-100 (nếu muốn dùng lâu dài)

### **Cài đặt Thư viện**

Sau khi cài Proteus:
1. Mở Proteus → **File → Open Library Manager**
2. Cài đặt các thư viện linh kiện:
   ```
   STM32F4 (STMicroelectronics)
   ESP32 / ESP32-S3 (Espressif)
   SSD1306 OLED Display
   DFPlayer Mini MP3 Module
   Relay, Transistor BJT, Diode, LED, Resistor, Capacitor (cơ bản)
   ```

3. Nếu không tìm thấy, tải từ internet:
   - STM32: [STM32 CubeMX Models](https://www.st.com/en/development-tools/stm32cubemx.html)
   - Custom libraries: Download từ [SnapEDA](https://www.snapeda.com), [Ultra Librarian](https://www.ultralibrarian.com)

---

## 🎯 Bố cục Sơ đồ Nguyên lý

Sơ đồ tối ưu nên được chia thành **5 khối chính**:

```
┌─────────────────────────────────────────────────────────────┐
│                    Sơ đồ nguyên lý STM-FaceGuard             │
│                    (Trang 1 nếu vẽ trên 1 tờ A4)             │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌──────────┐         ┌──────────┐         ┌──────────┐    │
│  │STM32F411 │         │ESP32-S3  │         │ Camera   │    │
│  │+ Black   │←────────┤ N16R8    │←────────┤ OV3660   │    │
│  │Pill      │ UART1   │          │ DVP     │ 3MP      │    │
│  │100MHz    │ 115200  │ 240MHz   │         │ 1/4"     │    │
│  └──┬───┬───┘         └──────────┘         └──────────┘    │
│     │   │                                                   │
│     │   └─────────────────────┬──────────────────────────┐  │
│     │ GPIO PB0                │ GPIO4,5 (I2C SDA/SCL)   │  │
│     │                   ┌─────┴───────┐           ┌─────┴──┐│
│     │              ┌────┤  SSD1306    │           │ OV3660  ││
│     │              │    │  OLED 128x  │           │  I2C    ││
│     │              │    │  64 px, I2C │           │ Ctrl    ││
│     │              │    └─────────────┘           └─────────┘│
│     │                                                        │
│  RELAY CIRCUIT                    UART CIRCUIT              │
│  ┌────┐   ┌───────┐          ┌──────────┐    UART HEADER   │
│  │TX  │───┤ BC547 │          │DFPlayer  │    (Debug/Upload)│
│  │PB0 │   │ BJT   │──┐       │ Mini     │    PA2/PA3       │
│  └────┘   └───────┘  │       │ 9600 baud│    38400 baud    │
│               │      ├──────→ │ TX/RX    │    TX/RX         │
│            ┌──┴──┬───┴─────┐  │ UART6    │                  │
│            │ 12V │ 5V      │  └──────────┘    POWER DIST    │
│         ┌──┴─┬───┴──┬──────┘  AC Adapter  ┌────────────────┐│
│         │    │ +5V  │ +12V    DC 12V/2A   │ 12V → LDO/DC   ││
│         │    │ GND  │ RLY                  │ 5V, 3.3V Dist ││
│         │    └─┬────┘ INH                  └┬──────────┬───┬┘│
│         │      │      (Coil)                │          │   │
│         │      ├──────────────►Solenoid    GND        +5V +3.3V
│         │      │      (Relay)   12V/0.5A    
│         │      │      (K1)      Lock Ctl
│         │      │      SPDT
│         │      ├─────NO─────→ Solenoid +12V
│         │      └─────NC───────GND
│         │
│         └─Flyback Diode (1N4007) ────┘
│                                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 🖱️ Bước vẽ chi tiết

### **Bước 1: Tạo dự án mới**

1. Mở Proteus → **File → New Project**
2. Chọn **Schematic Capture (ISIS)** → OK
3. Đặt tên project: `STM-FaceGuard_Schematic`
4. Chọn nơi lưu: `thesis/figures/`

### **Bước 2: Thêm biểu tượng (Symbol) linh kiện**

Trong cửa sổ **Component List** bên trái:

1. **STM32F411CEU6** (Black Pill):
   - Tìm: `STM32F411CCU6` hoặc `STM32F4` (nếu không có F411 chính xác)
   - Click vào linh kiện → **P** (hoặc double-click) → Đặt trên tờ vẽ

2. **ESP32-S3-N16R8**:
   - Tìm: `ESP32-S3` hoặc tạo symbol custom (xem **Hướng dẫn Custom Symbol** phía dưới)
   - Đặt bên phải STM32

3. **Camera OV3660**:
   - Tìm: `OV3660` hoặc `CAMERA` → Đặt bên phải
   - Nếu không có, vẽ box đơn giản với dòng chữ "OV3660"

4. **OLED SSD1306**:
   - Tìm: `SSD1306` → Đặt dưới STM32
   - Nếu không có, tìm `DISPLAY` hoặc `LCD`

5. **DFPlayer Mini**:
   - Tìm: `DFPlayer` hoặc `MP3` → Đặt bên phải OLED
   - Nếu không có, vẽ box: "DFPlayer Mini"

6. **BJT (BC547A)**:
   - Tìm: `BC547` → Đặt để điều khiển relay

7. **Relay (5V 1-channel)**:
   - Tìm: `RELAY` hoặc `RL` → Chọn 5V 1 coil
   - Đặt bên cạnh BJT

8. **Diode (1N4007)**:
   - Tìm: `1N4007` hoặc `DIODE` → Đặt song song coil relay

9. **Resistor, Capacitor** (giá trị cụ thể sau)

10. **GND, VCC**: Tìm trong **Power** → Đặt sẵn

---

### **Bước 3: Vẽ bus từ / Power Distribution**

1. Tạo **Power Rail**:
   - Bên trái, vẽ các bus:
     - `+12V` (Adapter input)
     - `+5V` (từ DC-DC)
     - `+3.3V` (từ LDO)
     - `GND`

2. Kết nối các ICàng +3.3V, GND, +5V tương ứng:
   - STM32 → `+3.3V` + `GND`
   - ESP32-S3 → `+3.3V` + `GND` (có LDO riêng)
   - SSD1306 → `+3.3V` + `GND`
   - DFPlayer → `+5V` + `GND`

3. Vẽ **DC Power Input**:
   - Connector: `12V DC Input`
   - Đặt ở góc trái

---

### **Bước 4: Kết nối UART & I2C**

**UART1 (STM32 ↔ ESP32-S3)**:
- STM32: PA9 (USART1_TX) → ESP32: GPIO20 (RX)
- STM32: PA10 (USART1_RX) ← ESP32: GPIO19 (TX)
- GND ↔ GND (common)

**I2C1 (STM32 ↔ SSD1306)**:
- STM32: PB6 (I2C1_SCL) → SSD1306: SCL
- STM32: PB7 (I2C1_SDA) → SSD1306: SDA
- Pull-up: 4.7kΩ mỗi đường (tuỳ chọn, SSD1306 có sẵn)

**UART6 (STM32 ↔ DFPlayer)**:
- STM32: PA11 (USART6_TX) → DFPlayer: RX
- STM32: PA12 (USART6_RX) ← DFPlayer: TX

**Camera DVP (ESP32-S3 ← OV3660)**:
- Vẽ 8 đường data: Y[7:0]
- Vẽ control: PCLK, VSYNC, HREF, XCLK

---

### **Bước 5: Vẽ mạch Relay + Solenoid**

**Relay Control Circuit**:
```
        PA9
       (STM32)
        │
        ├───────→ [Text: "GPIO PB0"]
        │
        │
        ├──[R1: 10k pull-up]──→ +3.3V
        │
        ├───→ Base BJT (BC547)
              │
              ├──[R2: 1k]──→ +5V
              │
              Collector → Relay Coil +5V
              │
              Emitter → GND
        │
        └───→ [Relay K1 Coil]
              │
              ├──[D1: 1N4007 Flyback]
              │  (parallel, cathode to +5V)
              │
              └──→ GND
```

**Relay Contact (NC/NO/COM)**:
- **NO (Normally Open)** → Solenoid +12V
- **COM** → Common (power return)
- **NC (Normally Closed)** → Ground (nếu cần lưỡng lối)

---

### **Bước 6: Thêm nút nhấn (Button)**

Vẽ 3 nút nhấn:
- **BTN_ENROLL** (PA0): GND ↔ PA0 (pull-up nội)
- **BTN_DELETE** (PA1): GND ↔ PA1 (pull-up nội)
- **BTN_EXIT** (PC13): GND ↔ PC13 (pull-up nội)

Sơ đồ nút:
```
        +3.3V
           │
         [R: 10k]  (Pull-up, tuỳ chọn STM32 có sẵn)
           │
         ┌─┴─┐
         │ o───o  (Tactile switch SPST)
         └───┘
           │
          GND

          Pin STM32 (PA0, PA1, PC13) kết nối ở giữa
```

---

### **Bước 7: Thêm text & label**

1. **Tiêu đề**:
   - Insert → **Text** → "STM-FaceGuard Smart Lock - Face Recognition System"
   - Font: Arial, 20pt, Bold

2. **Nhãn từng khối**:
   - "STM32F411CEU6" (Microcontroller)
   - "ESP32-S3-N16R8" (AI Processor)
   - "OV3660 Camera" (Vision Input)
   - "SSD1306 OLED" (Display Output)
   - "DFPlayer Mini" (Audio Output)
   - "Solenoid Lock" (Actuator)

3. **Giá trị linh kiện**:
   - Resistor: 1k, 10k, 47k...
   - Capacitor: 0.1µF, 10µF, 100µF (tuỳ theo datasheet)

---

### **Bước 8: Kiểm tra lỗi (ERC)**

1. Vào **Tools → Electrical Rules Check (ERC)**
2. Chạy kiểm tra → Hệ thống sẽ báo:
   - ❌ **Unconnected**: Pin không kết nối (cần fix)
   - ❌ **Short circuit**: Dây nối nhầm (fix)
   - ⚠️ **Warning**: Cảnh báo (có thể bỏ qua nếu cố ý)

3. Fix tất cả lỗi ❌ trước khi export

---

### **Bước 9: Bố cục & Canh chỉnh**

1. **Sắp xếp component**:
   - Cluster theo chức năng (MCU, Sensor, Actuator)
   - Cách xa nhau ~2 cm để tránh sợi dây rối

2. **Orient component**:
   - Bấm **R** để rotate linh kiện
   - VCC thường nên ở trên, GND ở dưới
   - Signal input từ trái, output sang phải (chuẩn tục)

3. **Net routing**:
   - Double-click vào pin → kéo sợi dây
   - Sử dụng **Bus** (Multi-signal) cho các nhóm: Y[7:0], D[7:0]

---

## 📤 Xuất sơ đồ cho luận văn

### **Xuất PDF**

1. **File → Print Preview**
2. Chọn **PDF Printer** (hoặc "Save as PDF")
3. **Page setup**:
   - Size: **A4** (hoặc A3 nếu sơ đồ lớn)
   - Orientation: **Landscape**
   - Margins: 0.5 cm all sides

4. **Print** → **Đặt tên**: `STM-FaceGuard_Schematic.pdf`
5. Lưu vào `thesis/figures/`

### **Xuất PNG (cho web/preview)**

1. **File → Export Picture**
2. Chọn format: **PNG**
3. **Resolution**: 300 DPI (chất lượng cao)
4. **Save as**: `STM-FaceGuard_Schematic.png`

### **Chèn vào LaTeX**

Trong file `thesis/appendix/schematic.tex`:

```latex
\section{Sơ đồ nguyên lý}
\label{sec:schematic_diagram}

\begin{figure}[H]
  \centering
  \includegraphics[width=1.0\textwidth]{STM-FaceGuard_Schematic.pdf}
  \caption{Sơ đồ nguyên lý hoàn chỉnh hệ thống STM-FaceGuard 
           (Dual-MCU: STM32F411CEU6 + ESP32-S3 N16R8)}
  \label{fig:schematic_full}
\end{figure}

\vspace{1cm}

% Hoặc chia thành nhiều khối nếu sơ đồ quá lớn:

\subsection{Khối MCU điều khiển (STM32F411CEU6)}

\begin{figure}[H]
  \centering
  \includegraphics[width=0.85\textwidth]{stm32_block.pdf}
  \caption{Chi tiết khối STM32: GPIO, UART, I2C, Relay control}
  \label{fig:stm32_schematic}
\end{figure}

\subsection{Khối AI processor (ESP32-S3 + Camera)}

\begin{figure}[H]
  \centering
  \includegraphics[width=0.85\textwidth]{esp32_camera_block.pdf}
  \caption{Chi tiết khối ESP32-S3: Camera DVP, UART, power distribution}
  \label{fig:esp32_schematic}
\end{figure}
```

---

## 🛠️ Hướng dẫn Custom Symbol (Nếu linh kiện không có sẵn)

Ví dụ: Tạo symbol cho **ESP32-S3-N16R8** hoặc **DFPlayer Mini**

### **Tạo symbol mới**

1. **Library → Make New Library Part**
2. Chọn **Schematic Symbol**
3. **New Component**:
   - Name: `ESP32-S3-N16R8` (hoặc `DFPlayer_Mini`)
   - Prefix: `U` (cho IC) hoặc `J` (cho module)

4. **Vẽ hình chữ nhật (body)**:
   - **Rectangle Tool** → Vẽ box
   - Kích thước: 40mm × 30mm (tuỳ chọn)

5. **Thêm pin**:
   - Click vào cạnh box → **Add Pin** (hoặc Ctrl+P)
   - Nhập tên pin (VCC, GND, GPIO19, GPIO20, v.v.)
   - **Flip** pin để hướng ra ngoài

6. **Thêm label & chữ**:
   - Insert text: "ESP32-S3"

7. **Save Library** → File name: `STM-FaceGuard_Custom.lib`

---

## 🎯 Checklist sơ đồ hoàn chỉnh

- [ ] Tất cả linh kiện chính sắp xếp, dễ nhìn
- [ ] Tất cả pin kết nối đúng (UART TX↔RX, I2C SDA/SCL, GND common)
- [ ] Power distribution: +12V, +5V, +3.3V, GND rõ ràng
- [ ] ERC kiểm tra lỗi không có cảnh báo ❌
- [ ] Có text label từng khối chức năng
- [ ] Relay & BJT điều khiển solenoid vẽ đúng
- [ ] Nút nhấn (3 nút) có pull-up nếu cần
- [ ] Camera DVP 8-bit + control signal
- [ ] Flyback diode bảo vệ relay
- [ ] Xuất PDF 300 DPI cho luận văn

---

## 🐛 Troubleshooting

| Vấn đề | Giải pháp |
|--------|----------|
| **Không tìm thấy STM32F411** | Tìm `STM32F411CC` hoặc `STM32F4xx`, hoặc download từ ST datasheet |
| **ESP32-S3 không có symbol** | Tạo custom symbol (xem hướng dẫn phía trên) hoặc dùng box đơn giản |
| **DFPlayer Mini không tồn tại** | Vẽ box custom với pin: VCC, GND, TX, RX, DAC_L, DAC_R |
| **ERC báo "Floating net"** | Pin không kết nối - kiểm tra lại từng pin MCU, IC |
| **Sợi dây bị rối, khó đọc** | Sử dụng **Bus** (Ctrl+B) cho nhóm pin (Y[7:0], D[7:0]) |
| **Không sync giữa sơ đồ & code** | Kiểm tra lại pinout từ `main.h`, `STM-FaceGuard.ino` |
| **Print/PDF bị cắt** | Chỉnh **Page Size** = A3 hoặc chia sơ đồ thành nhiều trang |

---

## 📚 Tài liệu tham khảo

- **Proteus Official Manual**: Help → **Contents** trong Proteus
- **STM32F411 Datasheet**: [ST website](https://www.st.com/en/microcontrollers/stm32f411ce.html)
- **ESP32-S3 Datasheet**: [Espressif](https://www.espressif.com/en/products/socs/esp32-s3/overview)
- **SSD1306 Datasheet**: [Solomon Systech](https://www.solomon-systech.com/)
- **Online Schematic Tools**: [EasyEDA](https://easyeda.com) (thay thế Proteus, free)

---

## 💡 Lời khuyên cuối

1. **Bắt đầu đơn giản**: Vẽ MCU + 3 power rail trước, sau đó thêm từng khối
2. **Kiểm tra thường xuyên**: Không chờ đến cuối mới check, sẽ mất nhiều thời gian sửa
3. **Giữ sơ đồ sạch**: Dùng bus, labels, color coding (VCC đỏ, GND đen)
4. **Cross-check với code**: Mỗi pin trong sơ đồ phải match `main.h` và `.ino`
5. **Hỏi giảng viên**: Nếu không chắc pinout, hỏi giảng viên trước khi vẽ

---

**Ngày viết**: 26 tháng 3, 2026  
**Dành cho**: Dự án STM-FaceGuard Smart Lock  
**Công cụ**: Proteus 8.x  
**Mục đích**: Xuất sơ đồ PDF chất lượng cao cho luận văn
