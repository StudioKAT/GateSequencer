// オーディオブートローダのハード層（CH32V003 / ch32fun）
// ブートローダでハードに触れるのはこのファイルの実装（abl_hal.c）だけ
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "abl_packet.h"

void     ablHalInit(void);
// ステップ LED（bit(ch-1)）とパネル LED（bit(PanelLed)）を 595 に出す。同時に 165 を読む
void     ablHalLeds(uint16_t leds, uint8_t panel);
// 最後に ablHalLeds で読んだ 165 で、Push A と Push B が両方押されているか
bool     ablHalPushAB(void);
// SysTick（24MHz、ABL_TICKS_PER_US）
uint32_t ablHalTicks(void);
// PAUSE 入力のレベル（ジャック側の論理。トランジスタの反転を戻した値）
bool     ablHalPause(void);

// Flash の先頭（ablMetaValid などに渡す）
const uint8_t *ablHalFlash(void);
// Flash 操作（offset は Flash 先頭からの位置。64B 境界）。操作中は CPU が止まる
void     ablHalFlashUnlock(void);
void     ablHalErasePage(uint32_t offset);
void     ablHalWritePage(uint32_t offset, const uint8_t *data);
// 起動先をユーザー領域にしてソフトウェアリセットする（戻らない）
void     ablHalBootApp(void) __attribute__((noreturn));
