#ifndef __DFPLAYER_H
#define __DFPLAYER_H

#include "main.h"

/* MP3 track numbers on MicroSD (folder 01/) */
#define DFP_TRACK_OPEN_DOOR     1   /* "Xin chao, cua da mo" */
#define DFP_TRACK_DENIED        2   /* "Khong nhan dien duoc" */
#define DFP_TRACK_LOOK_AT_CAM  3   /* "Moi nhin vao camera"  */
#define DFP_TRACK_ENROLLED      4   /* "Da them thanh cong"   */
#define DFP_TRACK_DELETED       5   /* "Da xoa toan bo"       */

extern UART_HandleTypeDef huart3;

void DFPlayer_Init(void);
void DFPlayer_Play(uint8_t track);
void DFPlayer_SetVolume(uint8_t vol); /* 0 – 30 */

#endif /* __DFPLAYER_H */
