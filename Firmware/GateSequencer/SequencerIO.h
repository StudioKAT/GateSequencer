#pragma once
#include <stdint.h>
#include <stdbool.h>

// =============================================================================
// 定数・Enum 定義
// =============================================================================

// Pushボタンのインデックス定義（可読性向上のため）
enum PushIndex {
  PUSH_IDX_CLK = 1, // 旧 Push1: 手動クロック
  PUSH_IDX_A   = 2, // 旧 Push2: IN A 対応
  PUSH_IDX_B   = 3, // 旧 Push3: IN B 対応
  PUSH_IDX_C   = 4  // 旧 Push4: IN C 対応
};

// パネルLEDピン（アプリケーション側からも参照できるようにextern宣言）
extern const uint8_t PIN_PANEL_LED_CLK; // 旧 LED_A
extern const uint8_t PIN_PANEL_LED_A;   // 旧 LED_B
extern const uint8_t PIN_PANEL_LED_B;   // 旧 LED_C
extern const uint8_t PIN_PANEL_LED_C;   // 旧 LED_D

// =============================================================================
// 構造体定義
// =============================================================================

// トリガ最小幅シェーパ用構造体
// 短すぎるトリガ信号を引き延ばして出力するための管理用データ
struct TrigShaper {
  uint32_t minUs;       // 最小パルス幅(µs)
  bool     bypass;      // 入力が高速な場合はシェーピングをバイパスするフラグ
  bool     prevBase;    // 前回の入力レベル
  uint32_t lastRiseUs;  // 直近の立ち上がり時刻
  uint32_t prevRiseUs;  // その一つ前の立ち上がり時刻（周期計測用）
  uint32_t holdUntil;   // 出力をHighに保持する期限時刻
};

// =============================================================================
// 公開関数プロトタイプ
// =============================================================================

// システム初期化・入力ポーリング・PWM駆動
void seqBegin();
void seqPollInputs();      // 入力状態の更新（ループ内で定期的に呼ぶ）
void seqPwmService();      // PWM更新用（互換性のために残存、現在はタイマ駆動）

// スイッチ / Pushボタン 読み取り
//   seqReadSwitch: 1..18 → 0(下) / 1(中) / 2(上)
//   seqReadPush  : PushIndex または 1..4 → 押下中ならtrue
uint8_t seqReadSwitch(uint8_t idx1to18);
bool    seqReadPush(uint8_t idx1to4);

// 高速ゲート入力：現在の信号レベル取得
bool seqReadClkIn();
bool seqReadAIn();
bool seqReadBIn();
bool seqReadCIn();
bool seqReadPauseIn();

// 高速ゲート入力：エッジ検出（読み出し時にカウンタをクリアして返す）
// 割り込みでカウントしているため、極めて短いパルスも取りこぼさない
uint32_t seqTakeAndClearClkInRise();
uint32_t seqTakeAndClearClkInFall();
uint32_t seqTakeAndClearAInRise();
uint32_t seqTakeAndClearAInFall();
uint32_t seqTakeAndClearBInRise();
uint32_t seqTakeAndClearBInFall();
uint32_t seqTakeAndClearCInRise();
uint32_t seqTakeAndClearCInFall();
uint32_t seqTakeAndClearPauseRise();
uint32_t seqTakeAndClearPauseFall();

// OUT (Gate) 出力制御
void seqSetOutMask(uint16_t mask);         // 16bit一括設定
void seqGateWrite(uint8_t ch, bool level); // 個別チャンネル設定 (ch: 1..16)
void seqGateWriteMask(uint16_t mask);      // seqSetOutMaskのエイリアス
void seqAllGatesOff();                     // 全ゲートOFF

// TRIG 出力制御 (直結ピン)
void seqTrigA(bool level);
void seqTrigB(bool level);

// ステップLED制御 (ch:1..16, pct:0..100)
// PWM制御により輝度調整が可能
void seqLedSetPct(uint8_t ch, uint8_t pct);
void seqLedSetOffAll();