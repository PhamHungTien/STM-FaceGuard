#include "ssd1306.h"
#include "ssd1306_assets.h"

/* -----------------------------------------------------------------------
 * 5×7 ASCII font, characters 32 (space) … 126 (~)
 * Each entry = 5 bytes (columns), MSB = top pixel
 * ----------------------------------------------------------------------- */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* 32 space */
    {0x00,0x00,0x5F,0x00,0x00}, /* 33 !     */
    {0x00,0x07,0x00,0x07,0x00}, /* 34 "     */
    {0x14,0x7F,0x14,0x7F,0x14}, /* 35 #     */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* 36 $     */
    {0x23,0x13,0x08,0x64,0x62}, /* 37 %     */
    {0x36,0x49,0x55,0x22,0x50}, /* 38 &     */
    {0x00,0x05,0x03,0x00,0x00}, /* 39 '     */
    {0x00,0x1C,0x22,0x41,0x00}, /* 40 (     */
    {0x00,0x41,0x22,0x1C,0x00}, /* 41 )     */
    {0x14,0x08,0x3E,0x08,0x14}, /* 42 *     */
    {0x08,0x08,0x3E,0x08,0x08}, /* 43 +     */
    {0x00,0x50,0x30,0x00,0x00}, /* 44 ,     */
    {0x08,0x08,0x08,0x08,0x08}, /* 45 -     */
    {0x00,0x60,0x60,0x00,0x00}, /* 46 .     */
    {0x20,0x10,0x08,0x04,0x02}, /* 47 /     */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 48 0     */
    {0x00,0x42,0x7F,0x40,0x00}, /* 49 1     */
    {0x42,0x61,0x51,0x49,0x46}, /* 50 2     */
    {0x21,0x41,0x45,0x4B,0x31}, /* 51 3     */
    {0x18,0x14,0x12,0x7F,0x10}, /* 52 4     */
    {0x27,0x45,0x45,0x45,0x39}, /* 53 5     */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 54 6     */
    {0x01,0x71,0x09,0x05,0x03}, /* 55 7     */
    {0x36,0x49,0x49,0x49,0x36}, /* 56 8     */
    {0x06,0x49,0x49,0x29,0x1E}, /* 57 9     */
    {0x00,0x36,0x36,0x00,0x00}, /* 58 :     */
    {0x00,0x56,0x36,0x00,0x00}, /* 59 ;     */
    {0x08,0x14,0x22,0x41,0x00}, /* 60 <     */
    {0x14,0x14,0x14,0x14,0x14}, /* 61 =     */
    {0x00,0x41,0x22,0x14,0x08}, /* 62 >     */
    {0x02,0x01,0x51,0x09,0x06}, /* 63 ?     */
    {0x32,0x49,0x79,0x41,0x3E}, /* 64 @     */
    {0x7E,0x11,0x11,0x11,0x7E}, /* 65 A     */
    {0x7F,0x49,0x49,0x49,0x36}, /* 66 B     */
    {0x3E,0x41,0x41,0x41,0x22}, /* 67 C     */
    {0x7F,0x41,0x41,0x22,0x1C}, /* 68 D     */
    {0x7F,0x49,0x49,0x49,0x41}, /* 69 E     */
    {0x7F,0x09,0x09,0x09,0x01}, /* 70 F     */
    {0x3E,0x41,0x49,0x49,0x7A}, /* 71 G     */
    {0x7F,0x08,0x08,0x08,0x7F}, /* 72 H     */
    {0x00,0x41,0x7F,0x41,0x00}, /* 73 I     */
    {0x20,0x40,0x41,0x3F,0x01}, /* 74 J     */
    {0x7F,0x08,0x14,0x22,0x41}, /* 75 K     */
    {0x7F,0x40,0x40,0x40,0x40}, /* 76 L     */
    {0x7F,0x02,0x04,0x02,0x7F}, /* 77 M     */
    {0x7F,0x04,0x08,0x10,0x7F}, /* 78 N     */
    {0x3E,0x41,0x41,0x41,0x3E}, /* 79 O     */
    {0x7F,0x09,0x09,0x09,0x06}, /* 80 P     */
    {0x3E,0x41,0x51,0x21,0x5E}, /* 81 Q     */
    {0x7F,0x09,0x19,0x29,0x46}, /* 82 R     */
    {0x46,0x49,0x49,0x49,0x31}, /* 83 S     */
    {0x01,0x01,0x7F,0x01,0x01}, /* 84 T     */
    {0x3F,0x40,0x40,0x40,0x3F}, /* 85 U     */
    {0x1F,0x20,0x40,0x20,0x1F}, /* 86 V     */
    {0x3F,0x40,0x38,0x40,0x3F}, /* 87 W     */
    {0x63,0x14,0x08,0x14,0x63}, /* 88 X     */
    {0x07,0x08,0x70,0x08,0x07}, /* 89 Y     */
    {0x61,0x51,0x49,0x45,0x43}, /* 90 Z     */
    {0x00,0x7F,0x41,0x41,0x00}, /* 91 [     */
    {0x02,0x04,0x08,0x10,0x20}, /* 92 \     */
    {0x00,0x41,0x41,0x7F,0x00}, /* 93 ]     */
    {0x04,0x02,0x01,0x02,0x04}, /* 94 ^     */
    {0x40,0x40,0x40,0x40,0x40}, /* 95 _     */
    {0x00,0x01,0x02,0x04,0x00}, /* 96 `     */
    {0x20,0x54,0x54,0x54,0x78}, /* 97 a     */
    {0x7F,0x48,0x44,0x44,0x38}, /* 98 b     */
    {0x38,0x44,0x44,0x44,0x20}, /* 99 c     */
    {0x38,0x44,0x44,0x48,0x7F}, /* 100 d    */
    {0x38,0x54,0x54,0x54,0x18}, /* 101 e    */
    {0x08,0x7E,0x09,0x01,0x02}, /* 102 f    */
    {0x0C,0x52,0x52,0x52,0x3E}, /* 103 g    */
    {0x7F,0x08,0x04,0x04,0x78}, /* 104 h    */
    {0x00,0x44,0x7D,0x40,0x00}, /* 105 i    */
    {0x20,0x40,0x44,0x3D,0x00}, /* 106 j    */
    {0x7F,0x10,0x28,0x44,0x00}, /* 107 k    */
    {0x00,0x41,0x7F,0x40,0x00}, /* 108 l    */
    {0x7C,0x04,0x18,0x04,0x78}, /* 109 m    */
    {0x7C,0x08,0x04,0x04,0x78}, /* 110 n    */
    {0x38,0x44,0x44,0x44,0x38}, /* 111 o    */
    {0x7C,0x14,0x14,0x14,0x08}, /* 112 p    */
    {0x08,0x14,0x14,0x18,0x7C}, /* 113 q    */
    {0x7C,0x08,0x04,0x04,0x08}, /* 114 r    */
    {0x48,0x54,0x54,0x54,0x20}, /* 115 s    */
    {0x04,0x3F,0x44,0x40,0x20}, /* 116 t    */
    {0x3C,0x40,0x40,0x40,0x7C}, /* 117 u    */
    {0x1C,0x20,0x40,0x20,0x1C}, /* 118 v    */
    {0x3C,0x40,0x30,0x40,0x3C}, /* 119 w    */
    {0x44,0x28,0x10,0x28,0x44}, /* 120 x    */
    {0x0C,0x50,0x50,0x50,0x3C}, /* 121 y    */
    {0x44,0x64,0x54,0x4C,0x44}, /* 122 z    */
    {0x00,0x08,0x36,0x41,0x00}, /* 123 {    */
    {0x00,0x00,0x7F,0x00,0x00}, /* 124 |    */
    {0x00,0x41,0x36,0x08,0x00}, /* 125 }    */
    {0x10,0x08,0x08,0x10,0x08}, /* 126 ~    */
};

static uint8_t oled_fb[SSD1306_WIDTH * SSD1306_PAGES];

static void OLED_Cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    if (HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, 2, 10) != HAL_OK) {
        /* Recover once if the I2C bus stalls during a screen refresh. */
        HAL_I2C_DeInit(&hi2c1);
        HAL_Delay(2);
        HAL_I2C_Init(&hi2c1);
        HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, 2, 10);
    }
}

static void OLED_Data(const uint8_t *data, uint16_t len)
{
    uint8_t buf[129];
    if (len > 128U) {
        len = 128U;
    }
    buf[0] = 0x40;
    memcpy(&buf[1], data, len);
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, len + 1U, 50);
}

static void OLED_SetCursor(uint8_t col, uint8_t page)
{
    OLED_Cmd(0xB0 | (page & 0x07));
    OLED_Cmd(0x00 | (col & 0x0F));
    OLED_Cmd(0x10 | ((col >> 4) & 0x0F));
}

static void OLED_ClearBuffer(void)
{
    memset(oled_fb, 0, sizeof(oled_fb));
}

static void OLED_FlushPage(uint8_t page)
{
    if (page >= SSD1306_PAGES) {
        return;
    }

    OLED_SetCursor(0, page);
    OLED_Data(&oled_fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
}

static void OLED_Flush(void)
{
    for (uint8_t page = 0; page < SSD1306_PAGES; ++page) {
        OLED_FlushPage(page);
    }
}

static void OLED_SetPixel(int16_t x, int16_t y)
{
    uint16_t index;

    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }

    index = ((uint16_t)(y >> 3) * SSD1306_WIDTH) + (uint16_t)x;
    oled_fb[index] |= (uint8_t)(1U << (y & 0x07));
}

static void OLED_DrawHLine(uint8_t y, uint8_t x0, uint8_t x1)
{
    if (y >= SSD1306_HEIGHT) {
        return;
    }
    if (x0 > x1) {
        uint8_t tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    if (x1 >= SSD1306_WIDTH) {
        x1 = SSD1306_WIDTH - 1U;
    }
    for (uint8_t x = x0; x <= x1; ++x) {
        OLED_SetPixel(x, y);
    }
}

static void OLED_DrawFrame(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    if (w < 2U || h < 2U) {
        return;
    }

    OLED_DrawHLine(y, x, (uint8_t)(x + w - 1U));
    OLED_DrawHLine((uint8_t)(y + h - 1U), x, (uint8_t)(x + w - 1U));
    for (uint8_t row = y; row < (uint8_t)(y + h); ++row) {
        OLED_SetPixel(x, row);
        OLED_SetPixel((uint8_t)(x + w - 1U), row);
    }
}

static void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    for (uint8_t row = y; row < (uint8_t)(y + h); ++row) {
        for (uint8_t col = x; col < (uint8_t)(x + w); ++col) {
            OLED_SetPixel(col, row);
        }
    }
}

static void OLED_DrawBitmap(int16_t x, int16_t y, const SSD1306_Bitmap *bitmap)
{
    if (bitmap == NULL || bitmap->data == NULL) {
        return;
    }

    uint8_t bytes_per_row = (uint8_t)((bitmap->width + 7U) / 8U);
    for (uint8_t row = 0; row < bitmap->height; ++row) {
        for (uint8_t col = 0; col < bitmap->width; ++col) {
            uint8_t byte = bitmap->data[(row * bytes_per_row) + (col >> 3)];
            if ((byte & (uint8_t)(0x80U >> (col & 0x07U))) != 0U) {
                OLED_SetPixel((int16_t)(x + col), (int16_t)(y + row));
            }
        }
    }
}

static void OLED_DrawBitmapCentered(uint8_t y, const SSD1306_Bitmap *bitmap)
{
    int16_t x;

    if (bitmap == NULL) {
        return;
    }

    x = (int16_t)(SSD1306_WIDTH - bitmap->width) / 2;
    if (x < 0) {
        x = 0;
    }
    OLED_DrawBitmap(x, y, bitmap);
}

static void OLED_DrawCharPage(uint8_t x, uint8_t page, char c)
{
    uint16_t base;

    if (page >= SSD1306_PAGES || (x + CHAR_W) > SSD1306_WIDTH) {
        return;
    }
    if (c < 32 || c > 126) {
        c = ' ';
    }

    base = ((uint16_t)page * SSD1306_WIDTH) + x;
    memcpy(&oled_fb[base], font5x7[c - 32], 5);
    oled_fb[base + 5U] = 0x00;
}

static uint8_t OLED_TextWidth(const char *str)
{
    size_t len = (str != NULL) ? strlen(str) : 0U;
    size_t width = len * CHAR_W;

    if (width > SSD1306_WIDTH) {
        width = SSD1306_WIDTH;
    }
    return (uint8_t)width;
}

static void OLED_DrawStringPage(uint8_t x, uint8_t page, const char *str)
{
    uint8_t col = x;

    if (str == NULL || page >= SSD1306_PAGES) {
        return;
    }

    while (*str != '\0' && (col + CHAR_W) <= SSD1306_WIDTH) {
        OLED_DrawCharPage(col, page, *str++);
        col = (uint8_t)(col + CHAR_W);
    }
}

static void OLED_DrawCenteredStringPage(uint8_t page, const char *str)
{
    uint8_t width = OLED_TextWidth(str);
    uint8_t x = (width < SSD1306_WIDTH) ? (uint8_t)((SSD1306_WIDTH - width) / 2U) : 0U;
    OLED_DrawStringPage(x, page, str);
}

static void OLED_DrawBitmapTextRow(uint8_t y, const SSD1306_Bitmap *bitmap,
                                   uint8_t text_page, const char *text)
{
    uint16_t total_width = bitmap->width;
    uint8_t text_width = OLED_TextWidth(text);
    int16_t x;

    if (text != NULL && *text != '\0') {
        total_width = (uint16_t)(total_width + 4U + text_width);
    }

    x = (int16_t)(SSD1306_WIDTH - total_width) / 2;
    if (x < 0) {
        x = 0;
    }

    OLED_DrawBitmap(x, y, bitmap);
    if (text != NULL && *text != '\0') {
        OLED_DrawStringPage((uint8_t)(x + bitmap->width + 4U), text_page, text);
    }
}

static void OLED_BeginScreen(const SSD1306_Bitmap *main_bitmap, uint8_t main_y,
                             const SSD1306_Bitmap *sub_bitmap, uint8_t sub_y)
{
    OLED_ClearBuffer();
    OLED_DrawBitmapCentered(2, &oled_vi_title);
    OLED_DrawHLine(15, 10, 117);

    if (main_bitmap != NULL) {
        OLED_DrawBitmapCentered(main_y, main_bitmap);
    }
    if (sub_bitmap != NULL) {
        OLED_DrawBitmapCentered(sub_y, sub_bitmap);
    }
}

static void OLED_DrawProgressBar(uint8_t x, uint8_t y, uint8_t w, uint8_t h,
                                 uint8_t value, uint8_t max_value)
{
    uint8_t inner_w;
    uint8_t fill_w;

    if (w < 3U || h < 3U || max_value == 0U) {
        return;
    }

    if (value > max_value) {
        value = max_value;
    }

    OLED_DrawFrame(x, y, w, h);
    inner_w = (uint8_t)(w - 2U);
    fill_w = (uint8_t)(((uint16_t)inner_w * value) / max_value);
    if (fill_w > 0U) {
        OLED_FillRect((uint8_t)(x + 1U), (uint8_t)(y + 1U), fill_w, (uint8_t)(h - 2U));
    }
}

void SSD1306_Init(void)
{
    HAL_Delay(200);

    if (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 3, 100) != HAL_OK) {
        for (uint8_t i = 0; i < 20U; ++i) {
            HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
            HAL_Delay(100);
        }
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        return;
    }

    static const uint8_t init_seq[] = {
        0xAE,
        0xD5, 0x80,
        0xA8, 0x3F,
        0xD3, 0x00,
        0x40,
        0x8D, 0x14,
        0x20, 0x02,
        0xA1,
        0xC8,
        0xDA, 0x12,
        0x81, 0xCF,
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,
        0xA6,
        0xAF,
    };

    for (uint8_t i = 0; i < sizeof(init_seq); ++i) {
        OLED_Cmd(init_seq[i]);
    }

    OLED_ClearBuffer();
    OLED_Flush();
}

void SSD1306_FillPage(uint8_t page, uint8_t byte_val)
{
    if (page >= SSD1306_PAGES) {
        return;
    }

    memset(&oled_fb[page * SSD1306_WIDTH], byte_val, SSD1306_WIDTH);
    OLED_FlushPage(page);
}

void SSD1306_Clear(void)
{
    OLED_ClearBuffer();
    OLED_Flush();
}

void SSD1306_WriteString(uint8_t x, uint8_t page, const char *str)
{
    uint8_t col = x;

    if (page >= SSD1306_PAGES || x >= SSD1306_WIDTH) {
        return;
    }

    while ((col + CHAR_W) <= SSD1306_WIDTH) {
        char c = (str != NULL && *str != '\0') ? *str++ : ' ';
        OLED_DrawCharPage(col, page, c);
        col = (uint8_t)(col + CHAR_W);
    }

    OLED_FlushPage(page);
}

void SSD1306_ShowBoot(void)
{
    OLED_BeginScreen(&oled_vi_boot_main, 21, &oled_vi_boot_sub, 44);
    OLED_Flush();
}

void SSD1306_ShowConnecting(void)
{
    OLED_BeginScreen(&oled_vi_connect_main, 21, &oled_vi_connect_sub, 44);
    OLED_Flush();
}

void SSD1306_ShowESP32Offline(void)
{
    OLED_BeginScreen(&oled_vi_offline_main, 21, &oled_vi_offline_sub, 42);
    OLED_Flush();
}

void SSD1306_ShowReady(void)
{
    SSD1306_ShowReadyFaces(0);
}

void SSD1306_ShowReadyFaces(uint8_t count)
{
    char buf[8];

    if (count == 0U) {
        OLED_BeginScreen(&oled_vi_ready_main, 21, &oled_vi_ready_zero, 45);
    } else {
        snprintf(buf, sizeof(buf), "%u/7", count);
        OLED_BeginScreen(&oled_vi_ready_main, 21, NULL, 0);
        OLED_DrawBitmapTextRow(45, &oled_vi_ready_saved, 6, buf);
    }
    OLED_Flush();
}

void SSD1306_ShowDbFull(void)
{
    OLED_BeginScreen(&oled_vi_db_full_main, 21, &oled_vi_db_full_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowLockout(void)
{
    OLED_BeginScreen(&oled_vi_lock_main, 21, &oled_vi_lock_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowScanning(void)
{
    OLED_BeginScreen(&oled_vi_scan_main, 21, &oled_vi_scan_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowUnlocked(uint8_t id)
{
    char buf[10];

    OLED_BeginScreen(&oled_vi_open_main, 21, &oled_vi_open_exit_sub, 43);
    snprintf(buf, sizeof(buf), "ID %u", id);
    OLED_DrawCenteredStringPage(7, buf);
    OLED_Flush();
}

void SSD1306_ShowUnlockedExit(void)
{
    OLED_BeginScreen(&oled_vi_open_main, 21, &oled_vi_open_exit_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowDenied(void)
{
    OLED_BeginScreen(&oled_vi_denied_main, 21, &oled_vi_denied_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowEnrolling(void)
{
    OLED_BeginScreen(&oled_vi_enroll_main, 21, &oled_vi_enroll_sub, 43);
    OLED_Flush();
}

void SSD1306_ShowEnrollStep(uint8_t step, uint8_t total)
{
    const SSD1306_Bitmap *step_bitmap = &oled_vi_enroll_main;
    char buf[8];

    switch (step) {
        case 1: step_bitmap = &oled_vi_step1; break;
        case 2: step_bitmap = &oled_vi_step2; break;
        case 3: step_bitmap = &oled_vi_step3; break;
        case 4: step_bitmap = &oled_vi_step4; break;
        case 5: step_bitmap = &oled_vi_step5; break;
        default: break;
    }

    OLED_BeginScreen(step_bitmap, 21, &oled_vi_scan_sub, 43);
    snprintf(buf, sizeof(buf), "%u/%u", step, total);
    OLED_DrawCenteredStringPage(7, buf);
    OLED_Flush();
}

void SSD1306_ShowEnrolled(uint8_t id, uint8_t count, uint8_t max_count)
{
    char count_buf[8];
    char id_buf[10];

    OLED_BeginScreen(&oled_vi_enrolled_main, 21, NULL, 0);
    snprintf(count_buf, sizeof(count_buf), "%u/%u", count, max_count);
    OLED_DrawBitmapTextRow(43, &oled_vi_ready_saved, 6, count_buf);
    snprintf(id_buf, sizeof(id_buf), "ID %u", id);
    OLED_DrawCenteredStringPage(7, id_buf);
    OLED_Flush();
}

void SSD1306_ShowHoldDelete(uint8_t seconds)
{
    OLED_BeginScreen(&oled_vi_hold_main, 21, &oled_vi_hold_sub, 43);
    OLED_DrawProgressBar(16, 56, 96, 7, seconds, 3);
    OLED_Flush();
}

void SSD1306_ShowDeleting(void)
{
    OLED_BeginScreen(&oled_vi_deleting_main, 21, &oled_vi_deleting_sub, 44);
    OLED_Flush();
}

void SSD1306_ShowDeleted(void)
{
    OLED_BeginScreen(&oled_vi_deleted_main, 21, &oled_vi_deleted_sub, 44);
    OLED_Flush();
}
