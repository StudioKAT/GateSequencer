// シーケンサ共通部（ハード非依存）
// V1 GateSequencer.ino の共通ユーティリティを移植したもの。
// 時刻・乱数・入力はすべて引数で受け取り、ハードには触れない。
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "io_map.h"

#define PUSH_TRIG_US      2000  // 手動クロック時のトリガ幅
#define PUSH_DEBOUNCE_US  8000  // Push_CLK デバウンス時間
#define PUSH_LOCKOUT_US   20000 // Push A/B/C: 変化を受け付けた後、この間は次の変化を無視する（バウンス対策）
#define TRIG_MIN_US       3000  // TRIG 最小パルス幅

// 方向定義（SW17 の位置そのまま）
typedef enum { DIR_FORWARD = 0, DIR_PINGPONG = 1, DIR_RANDOM = 2 } DirMode;

// ハード層から受け取る 1 サイクル分の生入力
typedef struct {
  // 直結入力の立ち上がりエッジの回数（前回から何回あったか）
  // seqReadInputs は 1 回分だけ見る。複数回の処理は appUpdate が回数分ループして行う
  uint8_t clkRises, aRises, bRises, cRises;
  // 直結入力のレベル（反転補正済み・正論理）
  bool lvlClk, lvlA, lvlB, lvlC, lvlPause;
  // 165 チェーンの受信フレーム
  uint8_t in165[IO_FRAME_BYTES];
} RawInputs;

// モード処理に渡す 1 サイクル分の入力スナップショット
typedef struct {
  uint32_t nowUs;
  // エッジ検出（立ち上がり）
  bool clkRise, ainRise, binRise, cinRise;
  // レベル検出
  bool lvlClk, lvlAin, lvlBin, lvlCin, pause;
  // Push ボタン状態（レベル / 立ち上がり）
  bool pushClk, pushA, pushB, pushC;
  bool pushClkRise, pushARise, pushBRise, pushCRise;
  // スイッチ位置（SwPos）
  const uint8_t *sw; // SW1..16 = sw[0..15]（次の seqReadInputs まで有効）
  uint8_t swVersion;  // スイッチ・Push の解読結果が変わるたびに増える（モード側のキャッシュ判定用）
  uint8_t swDir;   // SW17
  uint8_t swMode;  // SW18
} SeqInputs;

// モード処理の出力。前回値を保持したまま更新する（V1 の 595 バッファと同じ扱い）
typedef struct {
  uint16_t outMask;  // bit(ch-1) = OUT ch
  uint16_t ledMask;      // bit(ch-1) = ステップ LED ch を強点灯
  uint16_t ledWeakMask;  // bit(ch-1) = ステップ LED ch を弱点灯（V1 の LED_PCT_WEAK 相当）
  bool     trigA, trigB;
  // パネル LED A..D（CLK / A / B / C）。E..G はアプリ層で決める
  bool     panelClk, panelA, panelB, panelC;
} SeqOutputs;

/* ---- 乱数（xorshift32） ---- */
void     seqRandSeed(uint32_t seed);
uint32_t seqRandom(uint32_t n);   // 0..n-1 を等確率で返す（n=0 なら 0）。割り算を使わない

/* ---- 入力スナップショット ---- */
// Push のエッジ検出・Push_CLK デバウンスの内部状態を初期化する
void seqInputsReset(void);
// Push_CLK のデバウンス状態だけを初期化する（モード開始時に V1 と同じく呼ぶ）
void seqDebounceReset(void);
void seqReadInputs(const RawInputs *raw, uint32_t nowUs, SeqInputs *in);
// raw の立ち上がり回数のうち edgeIndex 回目（0 始まり）のエッジだけを見る版
void seqReadInputsAt(const RawInputs *raw, uint8_t edgeIndex, uint32_t nowUs, SeqInputs *in);

/* ---- ステップ進行 ---- */
DirMode seqReadDirMode(uint8_t sw17);
// 直線 / PingPong のステップ進行計算
uint8_t seqNextStepLinear(uint8_t cur, DirMode mode, bool *forwardFlag, uint8_t maxSteps);
// ランダム方向の次ステップ: 現在のステップ以外の全ステップから一様に選ぶ
uint8_t seqNextRandom(uint8_t curStep, uint8_t maxSteps);

/* ---- TRIG 最小幅シェーパ ---- */
typedef struct {
  uint32_t minUs;       // 最小パルス幅(µs)
  bool     bypass;      // 入力が高速な場合はシェーピングをバイパスする
  bool     prevBase;    // 前回の入力レベル
  uint32_t lastRiseUs;  // 直近の立ち上がり時刻
  uint32_t prevRiseUs;  // その一つ前の立ち上がり時刻（周期計測用）
  bool     holding;     // 立ち上がりから minUs の保持中
} TrigShaper;

void seqTrigInit(TrigShaper *ts);
// 入力が短すぎても minUs 分だけ High を維持する
bool seqTrigShape(bool baseLevel, TrigShaper *ts, uint32_t nowUs);
