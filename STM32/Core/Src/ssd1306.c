#include "ssd1306.h"

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

/* ----------------------------------------------------------------------- */

static void OLED_Cmd(uint8_t cmd)
{
    uint8_t buf[2] = {0x00, cmd};
    if (HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, 2, 10) != HAL_OK) {
        /* I2C bus stuck – recover by reinitialising the peripheral */
        HAL_I2C_DeInit(&hi2c1);
        HAL_Delay(2);
        HAL_I2C_Init(&hi2c1);
        /* Retry once */
        HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, 2, 10);
    }
}

static void OLED_Data(const uint8_t *data, uint16_t len)
{
    /* Prepend 0x40 control byte using a local buffer (max 129 bytes) */
    uint8_t buf[129];
    if (len > 128) len = 128;
    buf[0] = 0x40;
    memcpy(&buf[1], data, len);
    HAL_I2C_Master_Transmit(&hi2c1, SSD1306_I2C_ADDR, buf, len + 1, 50);
}

static void OLED_SetCursor(uint8_t col, uint8_t page)
{
    OLED_Cmd(0xB0 | (page & 0x07));
    OLED_Cmd(0x00 | (col & 0x0F));
    OLED_Cmd(0x10 | ((col >> 4) & 0x0F));
}

/* ----------------------------------------------------------------------- */

void SSD1306_Init(void)
{
    HAL_Delay(200); /* Wait for OLED power-up */

    /* Diagnostic: if OLED does not ACK on I2C, blink LD2 (green LED, PA5)
     * 10 times rapidly then return.  No blinking = I2C OK. */
    if (HAL_I2C_IsDeviceReady(&hi2c1, SSD1306_I2C_ADDR, 3, 100) != HAL_OK) {
        for (uint8_t i = 0; i < 20; i++) {
            HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
            HAL_Delay(100);
        }
        HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);
        return; /* I2C not working – skip init */
    }

    static const uint8_t init_seq[] = {
        0xAE,       /* Display off                      */
        0xD5, 0x80, /* Clock: fosc, div=1               */
        0xA8, 0x3F, /* Multiplex: 64 rows               */
        0xD3, 0x00, /* Display offset: 0                */
        0x40,       /* Start line: 0                    */
        0x8D, 0x14, /* Charge pump: enabled             */
        0x20, 0x02, /* Mem mode: page addressing         */
        0xA1,       /* Segment remap col127=SEG0        */
        0xC8,       /* COM scan: remapped               */
        0xDA, 0x12, /* COM pins: alternative            */
        0x81, 0xCF, /* Contrast: 207                    */
        0xD9, 0xF1, /* Pre-charge                       */
        0xDB, 0x40, /* VCOM deselect                    */
        0xA4,       /* Show RAM content                 */
        0xA6,       /* Normal (not inverted)            */
        0xAF,       /* Display on                       */
    };

    for (uint8_t i = 0; i < sizeof(init_seq); i++) {
        OLED_Cmd(init_seq[i]);
    }
}

void SSD1306_FillPage(uint8_t page, uint8_t byte_val)
{
    uint8_t blank[128];
    memset(blank, byte_val, 128);
    OLED_SetCursor(0, page);
    OLED_Data(blank, 128);
}

void SSD1306_Clear(void)
{
    for (uint8_t p = 0; p < SSD1306_PAGES; p++) {
        SSD1306_FillPage(p, 0x00);
    }
}

/* Write a string starting at (x, page), padding remainder with spaces */
void SSD1306_WriteString(uint8_t x, uint8_t page, const char *str)
{
    uint8_t col = x;
    uint8_t char_buf[6];
    char_buf[5] = 0x00; /* Column gap always blank */

    OLED_SetCursor(col, page);

    while (col + CHAR_W <= SSD1306_WIDTH) {
        char c = (*str != '\0') ? *str++ : ' ';
        if (c < 32 || c > 126) c = ' ';
        memcpy(char_buf, font5x7[c - 32], 5);
        OLED_Data(char_buf, 6);
        col += CHAR_W;
    }
}

/* ----------------------------------------------------------------------- *
 * High-level screen helpers
 * Each function redraws only the changing pages (2-7).
 * Page 0: title bar (fixed after boot)
 * Page 1: separator line (fixed after boot)
 * Pages 2-3: main status
 * Pages 4-5: sub-status / detail
 * Pages 6-7: blank / extra info
 * ----------------------------------------------------------------------- */

static void draw_title(void)
{
    SSD1306_WriteString(0, 0, " >> STM-FaceGuard");
    SSD1306_FillPage(1, 0xFF); /* Solid separator line */
}

static void draw_status(const char *line2, const char *line3,
                         const char *line4, const char *line5)
{
    SSD1306_WriteString(0, 2, line2);
    SSD1306_WriteString(0, 3, line3);
    SSD1306_WriteString(0, 4, line4);
    SSD1306_WriteString(0, 5, line5);
    SSD1306_FillPage(6, 0x00);
    SSD1306_FillPage(7, 0x00);
}

void SSD1306_ShowBoot(void)
{
    SSD1306_Clear();
    draw_title();
    draw_status("", "   Booting...", "", "   Please wait");
}

void SSD1306_ShowConnecting(void)
{
    draw_status("", "  Connecting...", "", "   Please wait");
}

void SSD1306_ShowReady(void)
{
    draw_status("", "   ** READY **", "", "  Scan your face");
}

void SSD1306_ShowReadyFaces(uint8_t count)
{
    char buf[22];
    if (count == 0) {
        draw_status("", "   ** READY **", "", " Press [ENROLL]!");
    } else {
        snprintf(buf, sizeof(buf), "  %d face(s) stored", count);
        draw_status("", "   ** READY **", "", buf);
    }
}

void SSD1306_ShowDbFull(void)
{
    draw_status("", "  DB is FULL!", "", "Delete to enroll");
}

void SSD1306_ShowLockout(void)
{
    draw_status("", " !! LOCKED OUT !!", "", " Too many attempts");
}

void SSD1306_ShowScanning(void)
{
    draw_status("", "  Scanning...", "", "  Hold still...");
}

void SSD1306_ShowUnlocked(uint8_t id)
{
    char buf[22];
    draw_status("", " ** UNLOCKED **", "", "");
    snprintf(buf, sizeof(buf), "  Face ID: %d", id);
    SSD1306_WriteString(0, 5, buf);
}

void SSD1306_ShowUnlockedExit(void)
{
    draw_status("", " ** UNLOCKED **", "", "  (Exit button)");
}

void SSD1306_ShowDenied(void)
{
    draw_status("", " ** DENIED **", "", " Face not found!");
}

void SSD1306_ShowEnrolling(void)
{
    draw_status("", "  Enrolling...", "", " Look at camera!");
}

/*
 * Show guided enrollment step on OLED.
 *
 * step:  1=FRONT  2=LEFT  3=RIGHT  4=UP  5=DOWN
 * total: total number of steps (e.g. 5)
 *
 * Layout example (step 2 of 5, LEFT):
 *   Page 2:  "  Step 2 / 5    "
 *   Page 3:  "  Turn LEFT     "
 *   Page 4:  "  <---(O)       "   (ASCII arrow)
 *   Page 5:  "  Hold still... "
 */
void SSD1306_ShowEnrollStep(uint8_t step, uint8_t total)
{
    char header[22];
    snprintf(header, sizeof(header), "  Step %d / %d", step, total);

    const char *direction = "";
    const char *arrow     = "";

    switch (step) {
        case 1: direction = "  Look STRAIGHT "; arrow = "    --> (O) <-- "; break;
        case 2: direction = "  Turn LEFT     "; arrow = "  <--- (O)      "; break;
        case 3: direction = "  Turn RIGHT    "; arrow = "        (O) --->"; break;
        case 4: direction = "  Tilt UP       "; arrow = "       (O)      "; break;  /* up arrow via text */
        case 5: direction = "  Tilt DOWN     "; arrow = "       (O)      "; break;
        default: direction = "  Look at cam  "; arrow = "      (O)       "; break;
    }

    /* For UP/DOWN, annotate the arrow line differently */
    if (step == 4) arrow = "     /( O )     ";
    if (step == 5) arrow = "     \\( O )     ";

    SSD1306_WriteString(0, 2, header);
    SSD1306_WriteString(0, 3, direction);
    SSD1306_WriteString(0, 4, arrow);
    SSD1306_WriteString(0, 5, "  Hold still... ");
    SSD1306_FillPage(6, 0x00);
    SSD1306_FillPage(7, 0x00);
}

void SSD1306_ShowEnrolled(uint8_t id, uint8_t count, uint8_t max_count)
{
    char line4[22], line5[22];
    snprintf(line4, sizeof(line4), "  Face #%d saved!", id);
    snprintf(line5, sizeof(line5), "  (%d / %d slots used)", count, max_count);
    draw_status("", " ** ENROLLED **", line4, line5);
}

void SSD1306_ShowHoldDelete(uint8_t seconds)
{
    char buf[22];
    /* Progress bar: max 3 seconds → 10 chars */
    uint8_t prog = (seconds >= 3) ? 10 : (seconds * 10 / 3);
    char bar[11];
    for (uint8_t i = 0; i < 10; i++) bar[i] = (i < prog) ? '=' : '-';
    bar[10] = '\0';
    snprintf(buf, sizeof(buf), " [%s]", bar);
    draw_status("", " Hold 3s: Delete", "", buf);
}

void SSD1306_ShowDeleting(void)
{
    draw_status("", "  Deleting...", "", "  Please wait");
}

void SSD1306_ShowDeleted(void)
{
    draw_status("", " ** DELETED **", "", " All faces cleared");
}
