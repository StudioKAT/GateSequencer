#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

// オーディオブートローダの ch32fun 設定（1920B に収めるための設定を含む）
// PLL を使わず HSI 24MHz で動かす（SystemInit が小さくなる。Flash の待ちも 0 になる）
#define FUNCONF_USE_PLL 0
#define FUNCONF_SYSTEM_CORE_CLOCK 24000000
// SysTick を HCLK（24MHz）で回す（abl_demod の時刻の単位 ABL_TICKS_PER_US と合わせる）
#define FUNCONF_SYSTICK_USE_HCLK 1
// 割り込みを使わないので、ベクタテーブルを先頭のジャンプ 1 命令にする
#define FUNCONF_TINYVECTOR 1
// printf（SWIO）は使わない
#define FUNCONF_USE_DEBUGPRINTF 0

#endif
