// アプリ層: モード切替・パネル LED・595 フレーム生成（ハード非依存）
// V1 GateSequencer.ino の setup() / loop() に相当する
#pragma once
#include "seq_common.h"

typedef struct {
  // 論理出力
  uint16_t outMask;    // bit(ch-1) = OUT ch
  uint16_t ledMask;    // bit(ch-1) = ステップ LED ch を強点灯
  uint16_t ledWeakMask;// bit(ch-1) = ステップ LED ch を弱点灯
  uint8_t  panelMask;  // bit(PanelLed)
  bool     trigA, trigB;
  bool     outputsChanged; // 595 の出力（ゲート・ステップ LED・パネル LED）が前回から変わった
} AppOutputs;

// 電源投入時の初期化（Mode 0 で起動し、最初の appUpdate で SW18 のモードへ切り替わる）
void appInit(const RawInputs *raw, uint32_t nowUs, uint32_t seed);
// 1 サイクル分の処理
void appUpdate(const RawInputs *raw, uint32_t nowUs, AppOutputs *out);
// 現在のモード（0..2）
uint8_t appMode(void);
