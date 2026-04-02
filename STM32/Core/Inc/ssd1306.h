#ifndef __SSD1306_H
#define __SSD1306_H

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "main.h"

/* SSD1306 I2C address (7-bit 0x3C -> 8-bit write 0x78) */
#define SSD1306_I2C_ADDR    (0x3C << 1)
#define SSD1306_WIDTH       128
#define SSD1306_HEIGHT      64
#define SSD1306_PAGES       8
#define CHAR_W              6   /* 5 pixel cols + 1 gap */
#define CHARS_PER_ROW       21  /* 128 / 6 = 21 chars */

extern I2C_HandleTypeDef hi2c1;

void SSD1306_Init(void);
void SSD1306_Clear(void);
void SSD1306_WriteString(uint8_t x, uint8_t page, const char *str);
void SSD1306_FillPage(uint8_t page, uint8_t byte_val);

/* High-level screen functions */
void SSD1306_ShowBoot(void);
void SSD1306_ShowConnecting(void);
void SSD1306_ShowESP32Offline(void);
void SSD1306_ShowReady(void);
void SSD1306_ShowReadyFaces(uint8_t count);  /* Shows face count below READY */
void SSD1306_ShowDbFull(void);               /* DB at capacity, cannot enroll */
void SSD1306_ShowLockout(void);              /* Security lockout - too many failures */
void SSD1306_ShowLockedCountdown(uint32_t remaining_s); /* Lockout with countdown */
void SSD1306_ShowScanning(void);
void SSD1306_ShowUnlocked(uint8_t id);
void SSD1306_ShowUnlockedExit(void);
void SSD1306_ShowUnlockedRemote(void);
void SSD1306_ShowDenied(void);
void SSD1306_ShowEnrolling(void);
void SSD1306_ShowEnrollStep(uint8_t step, uint8_t total);
void SSD1306_ShowEnrolled(uint8_t id, uint8_t count, uint8_t max_count);
void SSD1306_ShowHoldDelete(uint8_t seconds);
void SSD1306_ShowDeleting(void);
void SSD1306_ShowDeleted(void);

#endif /* __SSD1306_H */
