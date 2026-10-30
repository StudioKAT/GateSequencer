// ハード層 API（CH32V003 / V2 基板）。実装は hal_ch32.c（PC テストではビルドしない）
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "seq_common.h"

// クロック・GPIO・SPI・EXTI・TIM2（BAM 送信）を初期化する。595 は全消灯で始まる
void halInit(void);

// µs 時刻（uint32 で自然に折り返す。V1 の micros() 相当）
uint32_t halMicros(void);

// 595 の出力を渡す。最大 20µs 後に TIM2 割り込みが BAM で送り始める
//   outMask: ゲート、ledFull / ledWeak: ステップ LED の強点灯 / 弱点灯、panelMask: パネル LED
// SPI（595 送信・165 受信）はすべて TIM2 割り込みが行い、処理ループは触らない
void halSetOutputs(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask);

// 165 から最後に受け取ったフレーム（1ms 以内に更新されている）
void halReadPanel(uint8_t in165[IO_FRAME_BYTES]);

// 直結入力（CLK / A / B / C / PAUSE）の前回呼び出し以降の立ち上がり回数と現在レベルを raw に書く
void halReadFastInputs(RawInputs *raw);

// TRIG A / B 出力
void halSetTrig(bool a, bool b);

// 乱数シード（未接続の PA2 の ADC ノイズと時刻から作る）
uint32_t halRandomSeed(void);
