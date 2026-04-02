#include "dfplayer.h"

#define DFP_CMD_GAP_MS          120U
#define DFP_POWERUP_DELAY_MS   1500U
#define DFP_RESET_DELAY_MS     1200U
#define DFP_POST_SELECT_MS      200U
#define DFP_POST_VOLUME_MS      200U
#define DFP_STOP_TO_PLAY_MS      80U
#define DFP_REPEAT_GUARD_MS     250U

/* DFPlayer Mini serial protocol (10-byte frame):
 * 0x7E  0xFF  0x06  CMD  FB  ParamH  ParamL  CkH  CkL  0xEF
 * Checksum = -(0xFF + 0x06 + CMD + FB + ParamH + ParamL)        */

static uint16_t dfp_checksum(uint8_t *buf6)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 6; i++) sum += buf6[i];
    return (uint16_t)(-(int16_t)sum);
}

static uint8_t  dfp_ready = 0U;
static uint8_t  dfp_last_track = 0xFFU;
static uint32_t dfp_last_cmd_tick = 0U;
static uint32_t dfp_last_play_tick = 0U;

static void dfp_wait_gap(void)
{
    uint32_t now;
    uint32_t elapsed;

    if (dfp_last_cmd_tick == 0U) return;

    now = HAL_GetTick();
    elapsed = now - dfp_last_cmd_tick;
    if (elapsed < DFP_CMD_GAP_MS) {
        HAL_Delay(DFP_CMD_GAP_MS - elapsed);
    }
}

static HAL_StatusTypeDef dfp_send(uint8_t cmd, uint8_t paramH, uint8_t paramL)
{
    uint8_t frame[10];
    HAL_StatusTypeDef status;

    dfp_wait_gap();

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

    status = HAL_UART_Transmit(&huart6, frame, 10, 100);
    if (status == HAL_OK) {
        dfp_last_cmd_tick = HAL_GetTick();
    }
    return status;
}

void DFPlayer_Init(void)
{
    dfp_ready = 0U;
    dfp_last_track = 0xFFU;
    dfp_last_cmd_tick = 0U;
    dfp_last_play_tick = 0U;

    HAL_Delay(DFP_POWERUP_DELAY_MS);      /* Wait for DFPlayer power-up           */
    dfp_send(0x0C, 0x00, 0x00);           /* Software reset                       */
    HAL_Delay(DFP_RESET_DELAY_MS);        /* Wait for SD enumeration after reset  */
    dfp_send(0x09, 0x00, 0x02);           /* Select TF card explicitly            */
    HAL_Delay(DFP_POST_SELECT_MS);
    dfp_send(0x06, 0x00, 30);             /* Keep original max volume             */
    HAL_Delay(DFP_POST_VOLUME_MS);
    dfp_send(0x16, 0x00, 0x00);           /* Stop any auto-play residue           */
    dfp_ready = 1U;
}

void DFPlayer_Play(uint8_t track)
{
    uint32_t now;

    if (!dfp_ready || (track == 0U)) return;

    now = HAL_GetTick();
    if ((track == dfp_last_track) && ((now - dfp_last_play_tick) < DFP_REPEAT_GUARD_MS)) {
        return;
    }

    /* Skip stop+delay if nothing is playing (different track or idle) - avoids
     * blocking App_Loop() with an unnecessary 80 ms HAL_Delay.              */
    if (dfp_last_track != 0xFFU) {
        dfp_send(0x16, 0x00, 0x00);
        HAL_Delay(DFP_STOP_TO_PLAY_MS);
    }
    if (dfp_send(0x03, 0x00, track) == HAL_OK) {
        dfp_last_track = track;
        dfp_last_play_tick = HAL_GetTick();
    }
}

void DFPlayer_Stop(void)
{
    if (!dfp_ready) return;
    dfp_send(0x16, 0x00, 0x00);
    dfp_last_track = 0xFFU;
}

void DFPlayer_SetVolume(uint8_t vol)
{
    if (!dfp_ready) return;
    if (vol > 30) vol = 30;
    dfp_send(0x06, 0x00, vol);
}
