#include "dfplayer.h"

/* DFPlayer Mini serial protocol (10-byte frame):
 * 0x7E  0xFF  0x06  CMD  FB  ParamH  ParamL  CkH  CkL  0xEF
 * Checksum = -(0xFF + 0x06 + CMD + FB + ParamH + ParamL)        */

static uint16_t dfp_checksum(uint8_t *buf6)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 6; i++) sum += buf6[i];
    return (uint16_t)(-(int16_t)sum);
}

static void dfp_send(uint8_t cmd, uint8_t paramH, uint8_t paramL)
{
    uint8_t frame[10];
    frame[0] = 0x7E;
    frame[1] = 0xFF;
    frame[2] = 0x06;
    frame[3] = cmd;
    frame[4] = 0x00;   /* no feedback */
    frame[5] = paramH;
    frame[6] = paramL;

    uint16_t cs = dfp_checksum(&frame[1]);
    frame[7] = (cs >> 8) & 0xFF;
    frame[8] =  cs       & 0xFF;
    frame[9] = 0xEF;

    HAL_UART_Transmit(&huart3, frame, 10, 100);
}

void DFPlayer_Init(void)
{
    HAL_Delay(1500);             /* Wait for DFPlayer boot + SD enumeration       */
    dfp_send(0x09, 0x00, 0x02); /* Select storage: SD card (before volume)        */
    HAL_Delay(500);              /* Wait for SD re-init (500ms sufficient for most cards) */
    dfp_send(0x06, 0x00, 25);   /* Set volume to 25/30                            */
    HAL_Delay(100);
}

void DFPlayer_Play(uint8_t track)
{
    dfp_send(0x03, 0x00, track); /* Play specific track number */
}

void DFPlayer_SetVolume(uint8_t vol)
{
    if (vol > 30) vol = 30;
    dfp_send(0x06, 0x00, vol);
}
