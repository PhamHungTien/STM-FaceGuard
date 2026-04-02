#ifndef __DFPLAYER_H
#define __DFPLAYER_H

#include "main.h"

/* MP3 track numbers on MicroSD
 * File naming: 0001.mp3 ... 0010.mp3 at SD card root (FAT32) */

/* --- System events --- */
#define DFP_TRACK_OPEN_DOOR     1   /* "Xin chao, cua da mo"               */
#define DFP_TRACK_DENIED        2   /* "Khong nhan dien duoc, thu lai"      */
#define DFP_TRACK_LOOK_AT_CAM   3   /* "Moi ban nhin vao camera"            */
#define DFP_TRACK_ENROLLED      4   /* "Da them khuon mat thanh cong"       */
#define DFP_TRACK_DELETED       5   /* "Da xoa toan bo du lieu khuon mat"   */

/* --- Enrollment step guidance (5 poses) --- */
#define DFP_TRACK_ENROLL_FRONT  6   /* "Moi nhin thang vao camera"          */
#define DFP_TRACK_ENROLL_LEFT   7   /* "Vui long quay dau sang trai"        */
#define DFP_TRACK_ENROLL_RIGHT  8   /* "Vui long quay dau sang phai"        */
#define DFP_TRACK_ENROLL_UP     9   /* "Vui long ngoc dau len mot chut"     */
#define DFP_TRACK_ENROLL_DOWN  10   /* "Vui long cui dau xuong mot chut"    */

extern UART_HandleTypeDef huart6;

void DFPlayer_Init(void);
void DFPlayer_Play(uint8_t track);
void DFPlayer_Stop(void);             /* Stop current playback */
void DFPlayer_SetVolume(uint8_t vol); /* 0 - 30 */

#endif /* __DFPLAYER_H */
