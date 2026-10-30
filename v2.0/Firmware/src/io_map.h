// V2 基板の 595 / 165 チェーンのビット配置とデコード（ハード非依存）
// 対応表の正本: docs/hardware.md
#pragma once
#include <stdint.h>
#include <stdbool.h>

// 595 / 165 とも 5 チップ = 40bit
#define IO_FRAME_BYTES 5

// スイッチの物理位置（V1 の seqReadSwitch() の戻り値と同じ値）
enum SwPos { SW_LEFT = 0, SW_CENTER = 1, SW_RIGHT = 2 };

// Push ボタン
enum PushIdx { PUSH_CLK = 0, PUSH_A = 1, PUSH_B = 2, PUSH_C = 3 };

// パネル LED（panelMask のビット番号）
enum PanelLed {
  PANEL_CLK = 0, PANEL_A, PANEL_B, PANEL_C,
  PANEL_PAUSE, PANEL_OUT_A, PANEL_OUT_B,
  PANEL_COUNT
};

// 595 へ送るフレームを組み立てる。frame[0] を最初に送る（MSB first）
//   outMask  : bit(ch-1) = OUT ch (1..16)
//   ledMask  : bit(ch-1) = ステップ LED ch (1..16)
//   panelMask: bit(PanelLed)
void ioBuildOutFrame(uint16_t outMask, uint16_t ledMask, uint8_t panelMask,
                     uint8_t frame[IO_FRAME_BYTES]);

// ステップ LED ch(1..16) の 595 フレーム上のビット位置（バイト番号 * 8 + ビット番号）
// ioBuildOutFrame と同じ配置。ブートローダ（1920B）が小さく LED を出すために使う
extern const uint8_t IO_STEP_LED_BIT[16];
// パネル LED（bit(PanelLed)）を置く 595 フレームのバイト（frame[0]）
static inline uint8_t ioPanelFrameByte(uint8_t panelMask) { return (uint8_t)((panelMask & 0x7Fu) << 1); } // LED_A..G = U12.QB..QH

// ステップ LED の明るさ（8bit、BAM の重み 1..128 の合計）
#define LED_DUTY_FULL 255  // 強点灯（100%）
#define LED_DUTY_WEAK 5    // 弱点灯（V1 の LED_PCT_WEAK = 2% → 5/255）
#define BAM_PLANES    8

// BAM（Bit Angle Modulation）の 8 区間ぶんの 595 フレームを組み立てる
//   区間 k（重み 2^k）では、明るさの bit k が立っているステップ LED だけを点灯させる
//   ゲート出力とパネル LED は PWM しないので、全区間で同じ値になる
//   ledFull / ledWeak: bit(ch-1) = ステップ LED ch を fullDuty / weakDuty で点灯（両方立っていれば ledFull を優先）
void ioBuildBamPlanes(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask,
                      uint8_t fullDuty, uint8_t weakDuty,
                      uint8_t planes[BAM_PLANES][IO_FRAME_BYTES]);
// 区間 k（0..7）の 1 フレームだけを組み立てる（TIM2 割り込みが送信のたびに呼ぶ）
void ioBuildBamPlane(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask,
                     uint8_t fullDuty, uint8_t weakDuty, uint8_t k,
                     uint8_t frame[IO_FRAME_BYTES]);

// 165 から受け取ったフレーム（in[0] が最初に受信したバイト）をデコードする
//   ioDecodeSwitch: sw 1..18 → SwPos（範囲外は SW_CENTER）
//   ioDecodePush  : 押下中なら true（LOW アクティブ）
uint8_t ioDecodeSwitch(const uint8_t in[IO_FRAME_BYTES], uint8_t sw);
bool    ioDecodePush(const uint8_t in[IO_FRAME_BYTES], uint8_t idx);
