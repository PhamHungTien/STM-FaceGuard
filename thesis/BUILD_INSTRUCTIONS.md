# Hướng dẫn Build LaTeX Luận văn STM-FaceGuard

## Yêu cầu

Cài đặt **TeX Live** hoặc **MiKTeX** với hỗ trợ XeLaTeX, Biber, và các package sau:
- `xelatex` (XeLaTeX engine)
- `biber` (Bibliography processor)
- `polyglossia` (Support tiếng Việt)
- `fontspec`, `titlesec`, `tocloft`, `booktabs`, `listings`, `tikz`, `pgfgantt`

### Hướng dẫn cài đặt (macOS/Linux)

```bash
# macOS (dùng Homebrew)
brew install basictex
tlmgr update --self
tlmgr install collection-xetex collection-langvietnamese \
  polyglossia fontspec titlesec tocloft booktabs listings \
  tikz pgfgantt fancyhdr geometry setspace cleveref

# Linux (Ubuntu/Debian)
sudo apt-get install texlive-xetex texlive-fonts-recommended texlive-fonts-extra
sudo apt-get install fonts-noto fonts-noto-cjk
tlmgr install collection-langvietnamese polyglossia fontspec ...
```

### Hướng dẫn cài đặt (Windows)

Tải và cài đặt [MiKTeX](https://miktex.org/) hoặc [TeX Live for Windows](https://www.tug.org/texlive/windows.html).

## Build PDF

### Cách 1: Sử dụng Makefile (macOS/Linux)

```bash
cd /Users/phamhungtien/Documents/STM-FaceGuard/thesis
make clean
make
```

Kết quả: `main.pdf` sẽ được tạo trong thư mục hiện tại.

### Cách 2: Build thủ công (đa nền tảng)

```bash
cd thesis/
xelatex -interaction=nonstopmode -synctex=1 main
biber main
xelatex -interaction=nonstopmode -synctex=1 main
xelatex -interaction=nonstopmode -synctex=1 main
```

**Giải thích:**
1. `xelatex` lần 1: Tạo `.aux`, `.bcf` file chứa metadata trích dẫn
2. `biber`: Xử lý tài liệu tham khảo từ `references.bib`
3. `xelatex` lần 2: Chèn tài liệu tham khảo vào PDF
4. `xelatex` lần 3: Cập nhật cross-reference (khi tài liệu thay đổi)

### Cách 3: Sử dụng Visual Studio Code + LaTeX Workshop Extension

1. Cài đặt extension `James Yu.latex-workshop` trong VSCode
2. Nhấp chuột phải vào `main.tex` → "Build LaTeX project"
3. PDF sẽ mở tự động trong VSCode Viewer

## Cấu trúc File

```
thesis/
├── main.tex                      # File chính (preamble + include)
├── chapters/
│   ├── 00_cover.tex              # Trang bìa
│   ├── 00_abstract.tex           # Tóm tắt tiếng Việt + English
│   ├── 00_abbreviations.tex      # Danh mục viết tắt
│   ├── 00_task_division.tex      # Phân công nhiệm vụ + GANTT
│   ├── ch1_intro.tex             # Chương 1: Giới thiệu
│   ├── ch2_theory.tex            # Chương 2: Cơ sở lý thuyết [SỬA CHỮA]
│   ├── ch3_hardware.tex          # Chương 3: Thiết kế phần cứng
│   ├── ch4_software.tex          # Chương 4: Thiết kế phần mềm [SỬA CHỮA]
│   ├── ch5_results.tex           # Chương 5: Kết quả thực nghiệm [SỬA CHỮA]
│   └── ch6_conclusion.tex        # Chương 6: Kết luận [SỬA CHỮA]
├── appendix/
│   ├── schematic.tex             # Sơ đồ nguyên lý (placeholder)
│   └── code_snippets.tex         # Đoạn mã nguồn minh họa
├── figures/                      # Thư mục chứa ảnh (hình vẽ, biểu đồ)
├── references.bib                # Tài liệu tham khảo (BibTeX format)
├── Makefile                      # Build script
├── main.pdf                      # Output PDF (được tạo sau build)
└── BUILD_INSTRUCTIONS.md         # File này
```

## Nội dung đã cải thiện

### 🔧 Những phần được sửa chữa (v2.0)

#### 1. **Chương 2 (Cơ sở lý thuyết)**
   - ✅ Cập nhật ngưỡng cosine similarity = 0.45 (thay vì 0.55)
   - ✅ Bổ sung chi tiết hai giai đoạn detection: MSR01 + MNP01
   - ✅ Sửa lỗi threshold description: từ 0.55 → 0.45
   - ✅ Cập nhật bảng UART protocol với các thông số chính: threshold, voting, timeout, lockout

#### 2. **Chương 4 (Thiết kế phần mềm)**
   - ✅ Sửa Enrollment: từ 5 tư thế → 1 tư thế (ENROLL_TOTAL_STEPS = 1 trong code)
   - ✅ Sửa Voting mechanism: từ 2 frames → 1 frame (REQUIRED_MATCHES = 1)
   - ✅ Thêm bảng thông số enrollment chi tiết
   - ✅ Chi tiết hóa Voting logic (Positive/Negative/Lockout)
   - ✅ Thêm giải thích khi nâng cấp voting lên 2-3 frames

#### 3. **Chương 5 (Kết quả thực nghiệm)**
   - ✅ Viết lại hoàn chỉnh với KPI rõ ràng (TPR, FAR, FNR, Latency, Uptime)
   - ✅ Bổ sung ma trận thí nghiệm chi tiết (6 điều kiện ánh sáng/khoảng cách)
   - ✅ Chi tiết hóa phân tích end-to-end latency thành các thành phần
   - ✅ Thêm bảng watchdog, giao tiếp UART DMA testing
   - ✅ Sửa bảng objectives evaluation với thêm 3 KPI quantitative

#### 4. **Chương 6 (Kết luận)**
   - ✅ Cập nhật 7 hạn chế thực tế, chi tiết, phù hợp với code
   - ✅ Viết lại hướng phát triển theo 3 mức ưu tiên (High/Medium/Low)
   - ✅ Bổ sung ý tưởng nâng cấp từ code (5-pose, voting tăng, IR illumination, liveness detection, etc.)

#### 5. **Abstract (Tóm tắt)**
   - ✅ Cập nhật threshold = 0.45 trong mô tả kết quả

### 📊 Các chỉ tiêu mới được bổ sung

| Chỉ tiêu | Vị trí | Ghi chú |
|----------|--------|--------|
| Cosine similarity threshold | Ch.2 + Ch.4 + Ch.5 | 0.45 (tunable) |
| Voting mechanism | Ch.2 + Ch.4 | 1 frame (REQUIRED_MATCHES=1) |
| Enrollment poses | Ch.4 | 1 (thích hợp cho current) |
| Max enrolled faces | Ch.2 + Ch.4 | 7 |
| Lockout duration | Ch.2 + Ch.6 | 5 phút / 5 lần sai |
| Latency decomposition | Ch.5 | Detection, Embedding, Matching, UART, Relay |
| Test matrix | Ch.5 | 6 điều kiện × 7 người × 30 lần = 1260 mẫu (theoretical) |
| KPI quantitative | Ch.5 | TPR ≥90%, FAR <5%, Latency <1.5s |

## Troubleshooting

### Lỗi: "xelatex: command not found"
**Giải pháp**: Cài đặt TeX Live hoặc MiKTeX (xem phần Yêu cầu)

### Lỗi: "Package polyglossia Error: No hyphenation patterns..."
**Giải pháp**: 
```bash
tlmgr install collection-langvietnamese
```

### Lỗi: "Undefined control sequence \..." trong PDF
**Giải pháp**: Kiểm tra import package thiếu trong `main.tex` preamble. Có thể thêm:
```latex
\usepackage{...} % hoặc \usepackage[]{...}
```

### PDF xuất hiện font khác (không phải Times New Roman)
**Giải pháp**: Cài đặt font system
```bash
# macOS
brew install font-times-new-roman

# Linux: Tải từ nước ngoài hoặc dùng font mặc định
```

### Lỗi khi build lần đầu: "E Cannot find biber"
**Giải pháp**:
```bash
# macOS
tlmgr install biber

# Linux
sudo apt-get install biber
```

## Lệnh hữu ích

```bash
# Xóa tất cả file tạm (cache, log, PDF)
make clean

# Build duy nhất
make

# Build + mở PDF (macOS)
make && open main.pdf

# Build verbose (hiển thị mọi thông báo)
xelatex -verbose main

# Đếm số từ trong PDF
pdftotext main.pdf - | wc -w

# Kiểm tra syntax LaTeX nhanh (không build full)
chktex main.tex
```

## Liên hệ & Hỗ trợ

Nếu gặp lỗi LaTeX, hãy:
1. Kiểm tra lại các package được cài đặt: `tlmgr list --only-installed | grep polyglossia`
2. Chạy `make clean && make` để rebuild từ đầu
3. Kiểm tra log: `main.log` (được tạo sau lần build đầu)
4. Tìm kiếm lỗi cụ thể trên [TeX Stack Exchange](https://tex.stackexchange.com)

---

**Last updated**: 26/3/2026  
**Version**: 2.0 (Comprehensive revision based on actual firmware code)
