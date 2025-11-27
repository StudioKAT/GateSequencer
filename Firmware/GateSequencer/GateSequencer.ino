#include "SequencerIO.h"

/*
  GateSequencer v1

  モード仕様
    0: 8step x2 シーケンサ (Dual Mode)
    1: 16step シーケンサ (Single Mode w/ Fill & Mute)
    2: シフトレジスタモード (Shift Register / Turing Machine style)

  共通 I/O マッピング
    CLK IN       : 外部クロック
    IN A         : Mode0/1=汎用, Mode2=DATA IN
    IN B         : リセット
    IN C         : Mode2=XOR DATA IN
    PAUSE        : 一時停止

    Push_CLK     : 手動クロック (全モード共通)
    Push_A       : Mode0=Sync切替, Mode1=FILL, Mode2=DATA手動入力
    Push_B       : リセット (全モード共通)
    Push_C       : Mode0=SyncRst切替, Mode1=MUTE, Mode2=DATA削除

    SW1〜16      : 各ステップ設定 (OFF / PROB / ON)
    SW17         : 方向選択 (0:Forward / 1:PingPong / 2:Random)
    SW18         : モード選択 (0 / 1 / 2)

  パネルLED
    LED_CLK      : CLK状態
    LED_A        : IN A / Push A 状態
    LED_B        : IN B / Push B 状態
    LED_C        : IN C / Push C 状態
*/

// =========================================================
// 基本定数・設定
// =========================================================

#define MAX_STEPS_MODE0   8
#define MAX_STEPS_MODE1   16
#define PUSH_TRIG_US      2000  // 手動クロック時のトリガ幅
#define PUSH_DEBOUNCE_US  8000  // Push_CLK デバウンス時間

// グローバル状態
static uint8_t g_mode = 0; // 現在のモード (0, 1, 2)

// 方向定義
enum DirMode : uint8_t { DIR_FORWARD=0, DIR_PINGPONG=1, DIR_RANDOM=2 };

// =========================================================
// 共通シーケンサ状態管理用構造体
// =========================================================

struct SeqState {
  uint8_t  step = 0;
  bool     forward = true;
  bool     gateLatched = false;
  bool     pushPulseActive = false; // 手動クロック時のトリガ生成用
  uint32_t pushPulseOffUs = 0;
};
static SeqState seqA, seqB;

// Push_CLK 専用デバウンス管理
struct Debounce {
  bool     rawPrev = false;
  bool     stable  = false;
  uint32_t lastChangeUs = 0;
} s_debouncePushClk;

// 1サイクル分の全入力スナップショット
struct Inputs {
  // エッジ検出 (立ち上がり)
  bool clkRise, ainRise, binRise, cinRise;
  // レベル検出
  bool lvlClk, lvlAin, lvlBin, lvlCin, pause;
  // Pushボタン状態 (レベル)
  bool pushClk, pushA, pushB, pushC;
  // Pushボタン状態 (立ち上がり)
  bool pushClkRise, pushARise, pushBRise, pushCRise;
  // 設定スイッチ
  uint8_t swDir;   // SW17
  uint8_t swMode;  // SW18
};

// ボタンの立ち上がり検出用履歴
static bool s_prevPushA=false, s_prevPushB=false, s_prevPushC=false;

// =========================================================
// 共通ユーティリティ関数
// =========================================================

// スイッチの値 (0/1/2) から確率でTrue/Falseを返す
// 2(上): 100%, 1(中): 50%, 0(下): 0%
static inline bool chanceFromSwitch(uint8_t swVal) {
  if (swVal >= 2) return true;
  if (swVal == 1) return (random(0,100) < 50);
  return false;
}

static inline DirMode readDirMode(uint8_t sw17) {
  return (DirMode)constrain(sw17, 0, 2);
}

// 直線／PingPong のステップ進行計算
static uint8_t nextStepLinear(uint8_t cur, DirMode mode, bool &forwardFlag, uint8_t maxSteps) {
  switch (mode) {
    case DIR_FORWARD:
      return (uint8_t)((cur + 1) % maxSteps);

    case DIR_PINGPONG:
    default:
      if (forwardFlag) {
        if (cur < maxSteps - 1) return cur + 1;
        forwardFlag = false;
        return cur;
      } else {
        if (cur > 0) return cur - 1;
        forwardFlag = true;
        return cur;
      }
  }
}

// Push_CLK デバウンス処理 (立ち上がりのみ検出)
static inline void debouncePushClk(bool raw, bool &stableOut, bool &riseOut) {
  const uint32_t now = micros();
  riseOut = false;
  if (raw != s_debouncePushClk.rawPrev) {
    s_debouncePushClk.rawPrev = raw;
    s_debouncePushClk.lastChangeUs = now;
  }
  if ((uint32_t)(now - s_debouncePushClk.lastChangeUs) >= PUSH_DEBOUNCE_US) {
    if (s_debouncePushClk.stable != raw) {
      s_debouncePushClk.stable = raw;
      if (s_debouncePushClk.stable) riseOut = true;
    }
  }
  stableOut = s_debouncePushClk.stable;
}

// パネルLED輝度制御ヘルパー (0-100% -> analogWrite)
static inline uint8_t pctTo8bit(uint8_t pct){
  uint32_t v = (uint32_t)pct * 255u + 50u;
  if (v > 25500u) v = 25500u;
  return (uint8_t)(v / 100u);
}
static inline void panelLedWrite(uint8_t pin, uint8_t pct){
  analogWrite(pin, pctTo8bit(pct));
}

// ステップLED制御ヘルパー
static inline void ledAllOff(){ seqLedSetOffAll(); }
static inline void ledSetWeak(uint8_t ch){ seqLedSetPct(ch, 1); }   // 待機状態
static inline void ledSetFull(uint8_t ch){ seqLedSetPct(ch, 100); } // アクティブ

// =========================================================
// 入力読み出し (SequencerIO利用)
// =========================================================

static Inputs readInputs() {
  Inputs in{};

  // 高速入力のエッジ取得
  in.clkRise = (seqTakeAndClearClkInRise() > 0);
  in.ainRise = (seqTakeAndClearAInRise()   > 0);
  in.binRise = (seqTakeAndClearBInRise()   > 0);
  in.cinRise = (seqTakeAndClearCInRise()   > 0);

  // レベル取得
  in.lvlClk = seqReadClkIn();
  in.lvlAin = seqReadAIn();
  in.lvlBin = seqReadBIn();
  in.lvlCin = seqReadCIn();
  in.pause  = seqReadPauseIn();

  // Push_CLK (デバウンスあり)
  bool pClkStable=false, pClkRise=false;
  debouncePushClk(seqReadPush(PUSH_IDX_CLK), pClkStable, pClkRise);
  in.pushClk     = pClkStable;
  in.pushClkRise = pClkRise;

  // Push A/B/C (デバウンスなし・即応性重視)
  in.pushA = seqReadPush(PUSH_IDX_A);
  in.pushB = seqReadPush(PUSH_IDX_B);
  in.pushC = seqReadPush(PUSH_IDX_C);

  // 立ち上がり検出
  in.pushARise = (in.pushA && !s_prevPushA);
  in.pushBRise = (in.pushB && !s_prevPushB);
  in.pushCRise = (in.pushC && !s_prevPushC);
  s_prevPushA = in.pushA;
  s_prevPushB = in.pushB;
  s_prevPushC = in.pushC;

  // 設定スイッチ
  in.swDir  = seqReadSwitch(17);
  in.swMode = seqReadSwitch(18);
  return in;
}

// =========================================================
// トリガ整形ロジック
// =========================================================

static TrigShaper s_trigShapeA, s_trigShapeB;

static inline void trigInit(TrigShaper &ts){
  ts.minUs      = 3000;  // 3ms確保
  ts.bypass     = false;
  ts.prevBase   = false;
  ts.lastRiseUs = 0;
  ts.prevRiseUs = 0;
  ts.holdUntil  = 0;
}

// 入力が短すぎても minUs 分だけHighを維持する
static inline bool trigShape(bool baseLevel, TrigShaper &ts){
  const uint32_t now = micros();
  const bool baseRise =  baseLevel && !ts.prevBase;
  ts.prevBase = baseLevel;

  if (baseRise) {
    ts.prevRiseUs = ts.lastRiseUs;
    ts.lastRiseUs = now;
    // 入力周期が短すぎる場合は整形を諦めてスルーする（追従性重視）
    if (ts.prevRiseUs != 0 && (uint32_t)(now - ts.prevRiseUs) < ts.minUs) {
      ts.bypass = true;
    } else {
      ts.bypass = false;
    }
    ts.holdUntil = now + ts.minUs;
  }

  if (ts.bypass) {
    return baseLevel;
  } else {
    if (baseLevel) return true;
    return (int32_t)(ts.holdUntil - now) > 0;
  }
}

// =========================================================
// Mode 0: 8step x2 Sequencer (A/B)
// =========================================================

static bool s_m0_syncAB  = false; // Push_A: Bの進行をAのクロックに同期
static bool s_m0_syncRst = false; // Push_C: BのリセットをAと同期

static uint8_t s_m0_randVisitedA = 0;
static uint8_t s_m0_randVisitedB = 0;

// そのステップでゲートを出力するか判定
static inline bool mode0_latchGate(uint8_t step0, uint8_t baseIdx) {
  const uint8_t swIdx = baseIdx + step0;
  return chanceFromSwitch(seqReadSwitch(swIdx));
}

// ランダムモード用：全ステップを踏破するまでリセットしないランダム
static uint8_t mode0_nextRandom(bool isA, uint8_t curStep){
  uint8_t &visited   = isA ? s_m0_randVisitedA : s_m0_randVisitedB;
  const uint8_t base = isA ? 1 : 9; // SW1..8 or SW9..16

  // 有効なステップ（スイッチがOFF以外）を抽出
  uint8_t enabledMask = 0;
  for (uint8_t i=0; i<MAX_STEPS_MODE0; ++i){
    uint8_t swVal = seqReadSwitch(base + i);
    if (swVal >= 1) enabledMask |= (uint8_t)(1u << i); // 中or上で有効とみなす
  }

  // 全てOFFなら純粋ランダム
  if (!enabledMask) {
    uint8_t nxt;
    do { nxt = (uint8_t)random(0, MAX_STEPS_MODE0); } while (nxt == curStep);
    visited = 0;
    return nxt;
  }

  visited &= enabledMask;
  uint8_t candidateMask = enabledMask & (uint8_t)~(1u << curStep);

  // 全て訪問済みならリセット
  if (!candidateMask) {
    visited = 0;
    candidateMask = enabledMask & (uint8_t)~(1u << curStep);
    if (!candidateMask) return curStep;
  }

  // 候補からランダム選択
  uint8_t count = 0;
  for (uint8_t i=0; i<MAX_STEPS_MODE0; ++i){
    if (candidateMask & (1u << i)) ++count;
  }
  uint8_t k = (uint8_t)random(0, count);
  uint8_t chosen = curStep;
  for (uint8_t i=0; i<MAX_STEPS_MODE0; ++i){
    if (candidateMask & (1u << i)) {
      if (k == 0) { chosen = i; break; }
      --k;
    }
  }
  visited |= (uint8_t)(1u << chosen);
  return chosen;
}

static void mode0_begin() {
  seqA = SeqState{};
  seqB = SeqState{};
  seqA.gateLatched = mode0_latchGate(seqA.step, 1);
  seqB.gateLatched = mode0_latchGate(seqB.step, 9);
  s_m0_syncAB  = false;
  s_m0_syncRst = false;
  s_debouncePushClk = Debounce{};
  trigInit(s_trigShapeA);
  trigInit(s_trigShapeB);
  s_m0_randVisitedA = 0;
  s_m0_randVisitedB = 0;
  seqSetOutMask(0);
  ledAllOff();
}

static void mode0_update(const Inputs& in) {
  const DirMode dirMode = readDirMode(in.swDir);

  // パネルLED表示
  panelLedWrite(PIN_PANEL_LED_CLK, (in.lvlClk || in.pushClk) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_A,   s_m0_syncAB ? 100 : ((in.lvlAin || in.pushA) ? 100 : 0));
  panelLedWrite(PIN_PANEL_LED_B,   (in.lvlBin || in.pushB) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_C,   s_m0_syncRst ? 100 : ((in.lvlCin || in.pushC) ? 100 : 0));

  // 設定切り替え
  if (in.pushARise) s_m0_syncAB  = !s_m0_syncAB;
  if (in.pushCRise) s_m0_syncRst = !s_m0_syncRst;

  if (in.pause) {
    seqTrigA(false);
    seqTrigB(false);
    return;
  }

  // リセット判定
  const bool resetA = (in.binRise || in.pushB); // Push_B or IN B
  const bool resetB = (in.cinRise || (s_m0_syncRst && resetA));

  if (resetA) {
    seqA.step = 0;
    seqA.forward = true;
    seqA.gateLatched = mode0_latchGate(seqA.step, 1);
    s_m0_randVisitedA = 0;
  }
  if (resetB) {
    seqB.step = 0;
    seqB.forward = true;
    seqB.gateLatched = mode0_latchGate(seqB.step, 9);
    s_m0_randVisitedB = 0;
  }

  // クロック進行判定
  const bool clkEvent = (in.clkRise || in.pushClkRise);
  const bool advA = clkEvent;
  const bool advB = (s_m0_syncAB ? clkEvent : in.ainRise);

  if (advA) {
    if (dirMode == DIR_RANDOM) {
      seqA.step = mode0_nextRandom(true, seqA.step);
    } else {
      seqA.step = nextStepLinear(seqA.step, dirMode, seqA.forward, MAX_STEPS_MODE0);
    }
    seqA.gateLatched = mode0_latchGate(seqA.step, 1);
    if (in.pushClkRise) {
      seqA.pushPulseActive = true;
      seqA.pushPulseOffUs = micros() + PUSH_TRIG_US;
    }
  }
  if (advB) {
    if (dirMode == DIR_RANDOM) {
      seqB.step = mode0_nextRandom(false, seqB.step);
    } else {
      seqB.step = nextStepLinear(seqB.step, dirMode, seqB.forward, MAX_STEPS_MODE0);
    }
    seqB.gateLatched = mode0_latchGate(seqB.step, 9);
  }

  // OUT出力設定
  uint16_t outMask = 0;
  if (seqA.gateLatched) outMask |= (1u << seqA.step);
  if (seqB.gateLatched) outMask |= (1u << (seqB.step + 8));
  seqSetOutMask(outMask);

  // ステップLED更新
  ledAllOff();
  if (seqA.gateLatched) ledSetFull(seqA.step + 1);
  else                  ledSetWeak(seqA.step + 1);
  if (seqB.gateLatched) ledSetFull(seqB.step + 9);
  else                  ledSetWeak(seqB.step + 9);

  // トリガ出力整形
  const bool baseA   = (seqA.gateLatched && in.lvlClk) || seqA.pushPulseActive;
  const bool baseB   = (seqB.gateLatched && (s_m0_syncAB ? in.lvlClk : in.lvlAin));
  const bool shapedA = trigShape(baseA, s_trigShapeA);
  const bool shapedB = trigShape(baseB, s_trigShapeB);
  seqTrigA(shapedA);
  seqTrigB(shapedB);

  if (seqA.pushPulseActive && (int32_t)(micros() - seqA.pushPulseOffUs) >= 0) {
    seqA.pushPulseActive = false;
  }
}

// =========================================================
// Mode 1: 16step Sequencer (FILL / MUTE)
// =========================================================

static struct {
  uint8_t step = 0;
  bool    forward = true;
  bool    gateLatched = false;
} s_m1;

static uint16_t s_m1_randVisited = 0;

static inline uint8_t mode1_swIndexForStep(uint8_t st) {
  // 1-8 -> SW1-8, 9-16 -> SW9-16
  return (st < 8) ? (uint8_t)(1 + st) : (uint8_t)(9 + (st - 8));
}
static inline bool mode1_latchGate(uint8_t st){
  return chanceFromSwitch(seqReadSwitch(mode1_swIndexForStep(st)));
}

static uint8_t mode1_nextRandom(uint8_t curStep){
  uint16_t enabledMask = 0;
  for (uint8_t st=0; st<MAX_STEPS_MODE1; ++st){
    uint8_t swVal = seqReadSwitch(mode1_swIndexForStep(st));
    if (swVal >= 1) enabledMask |= (uint16_t)(1u << st);
  }

  if (!enabledMask) {
    uint8_t nxt;
    do { nxt = (uint8_t)random(0, MAX_STEPS_MODE1); } while (nxt == curStep);
    s_m1_randVisited = 0;
    return nxt;
  }

  s_m1_randVisited &= enabledMask;
  uint16_t candidateMask = enabledMask & (uint16_t)~(1u << curStep);
  if (!candidateMask) {
    s_m1_randVisited = 0;
    candidateMask = enabledMask & (uint16_t)~(1u << curStep);
    if (!candidateMask) return curStep;
  }

  uint8_t count = 0;
  for (uint8_t st=0; st<MAX_STEPS_MODE1; ++st){
    if (candidateMask & (1u << st)) ++count;
  }
  uint8_t k = (uint8_t)random(0, count);
  uint8_t chosen = curStep;
  for (uint8_t st=0; st<MAX_STEPS_MODE1; ++st){
    if (candidateMask & (1u << st)) {
      if (k == 0) { chosen = st; break; }
      --k;
    }
  }
  s_m1_randVisited |= (uint16_t)(1u << chosen);
  return chosen;
}

static void mode1_begin() {
  s_m1 = {};
  s_debouncePushClk = Debounce{};
  s_m1.gateLatched = mode1_latchGate(s_m1.step);
  trigInit(s_trigShapeA);
  trigInit(s_trigShapeB);
  s_m1_randVisited = 0;
  seqSetOutMask(0);
  ledAllOff();
}

static void mode1_update(const Inputs& in) {
  const DirMode dirMode = readDirMode(in.swDir);

  panelLedWrite(PIN_PANEL_LED_CLK, (in.lvlClk || in.pushClk) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_A,   (in.lvlAin || in.pushA) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_B,   (in.lvlBin || in.pushB) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_C,   (in.lvlCin || in.pushC) ? 100 : 0);

  const bool clkLevel   = (in.lvlClk || in.pushClk);
  const bool fillActive = (in.lvlAin || in.pushA); // FILL機能
  const bool muteActive = (in.lvlCin || in.pushC); // MUTE機能

  if (in.pause) {
    seqTrigA(false);
    seqTrigB(false);
    return;
  }

  // リセット
  if (in.binRise || in.pushB) {
    s_m1.step = 0;
    s_m1.forward = true;
    s_m1.gateLatched = mode1_latchGate(s_m1.step);
    s_m1_randVisited = 0;
  }

  // クロック
  if (in.clkRise || in.pushClkRise) {
    if (dirMode == DIR_RANDOM) {
      s_m1.step = mode1_nextRandom(s_m1.step);
    } else {
      s_m1.step = nextStepLinear(s_m1.step, dirMode, s_m1.forward, MAX_STEPS_MODE1);
    }
    s_m1.gateLatched = mode1_latchGate(s_m1.step);
  }

  // ゲート出力判定 (Fill/Mute考慮)
  bool gateNow = s_m1.gateLatched;
  if (fillActive) gateNow = true;
  if (muteActive) gateNow = false;

  uint16_t outMask = 0;
  if (gateNow) {
    // Step 0-7 -> ch1-8, Step 8-15 -> ch9-16
    if (s_m1.step < 8) outMask |= (1u << s_m1.step);
    else               outMask |= (1u << (8 + (s_m1.step - 8)));
  }
  seqSetOutMask(outMask);

  ledAllOff();
  const uint8_t ch = (s_m1.step < 8) ? (uint8_t)(s_m1.step + 1)
                                     : (uint8_t)(s_m1.step - 8 + 9);
  if (gateNow) ledSetFull(ch);
  else         ledSetWeak(ch);

  // トリガ出力
  // A: 反転ゲート (Low Activeっぽい挙動)
  // B: 正論理ゲート
  const bool baseA   = (!gateNow) && clkLevel;
  const bool baseB   = ( gateNow) && clkLevel;
  const bool shapedA = trigShape(baseA, s_trigShapeA);
  const bool shapedB = trigShape(baseB, s_trigShapeB);
  seqTrigA(shapedA);
  seqTrigB(shapedB);
}

// =========================================================
// Mode 2: Shift Register (Variable Length & Routing)
// =========================================================

static struct {
  uint16_t sr = 0;   // 16bit シフトレジスタ実体
} s_m2;

// PingPong モード用トークン管理
struct M2Token {
  uint8_t pos;      // 有効ビット列上のインデックス
  int8_t  dir;      // +1:末尾へ, -1:先頭へ
  bool    tailHold; // 終端での滞留フラグ
  bool    headHold; // 先頭での滞留フラグ
};
static M2Token s_m2_tokens[16];
static uint8_t s_m2_tokenCount = 0;

// ランダムルーティング管理
static uint16_t s_m2_randEnabledMask = 0;
static uint8_t  s_m2_randRoute[16];
static uint8_t  s_m2_randLen = 0;

static DirMode s_m2_prevDir = DIR_FORWARD;

// 有効ビットリスト構築 (スイッチが中or上のステップのみ経由する)
static void mode2_buildEnabledList(uint8_t *idxList, uint8_t &n, uint16_t &enabledMask){
  n = 0;
  enabledMask = 0;
  for (uint8_t i=0; i<16; ++i){
    uint8_t sw = seqReadSwitch(i+1); // SW1..16
    if (sw >= 1){
      idxList[n++] = i;
      enabledMask |= (uint16_t)(1u << i);
    }
  }
}

// ランダムルーティング再構築
static void mode2_rebuildRandomRoute(const uint8_t *idxList, uint8_t n, uint16_t enabledMask){
  s_m2_randLen         = n;
  s_m2_randEnabledMask = enabledMask;

  if (n == 0) return;
  // 端点は固定、中間のみシャッフルするロジック
  if (n == 1) { s_m2_randRoute[0] = idxList[0]; return; }
  if (n == 2) { s_m2_randRoute[0] = idxList[0]; s_m2_randRoute[1] = idxList[1]; return; }

  s_m2_randRoute[0]   = idxList[0];
  s_m2_randRoute[n-1] = idxList[n-1];

  uint8_t tmp[16];
  uint8_t m = (uint8_t)(n - 2);
  for (uint8_t i=0; i<m; ++i) tmp[i] = idxList[i+1];

  // Fisher-Yates shuffle
  for (int k=(int)m-1; k>0; --k){
    int j = (int)random(0, k+1);
    uint8_t t = tmp[k];
    tmp[k] = tmp[j];
    tmp[j] = t;
  }
  for (uint8_t i=0; i<m; ++i) s_m2_randRoute[1+i] = tmp[i];
}

// 方向切り替え時の状態引継ぎ
static void mode2_onDirChanged(DirMode oldDir, DirMode newDir){
  if (oldDir == newDir) return;

  if (newDir == DIR_PINGPONG) {
    // 直線→ピンポン：現在ONのビットをトークン化して動きを与える
    uint8_t  idxList[16];
    uint8_t  n = 0;
    uint16_t enabledMask = 0;
    mode2_buildEnabledList(idxList, n, enabledMask);

    s_m2_tokenCount = 0;
    if (n == 0) return;

    for (uint8_t k=0; k<n && s_m2_tokenCount<16; ++k) {
      uint8_t idx = idxList[k];
      bool bitOn = ((s_m2.sr >> idx) & 1u) != 0;
      if (bitOn) {
        M2Token &t = s_m2_tokens[s_m2_tokenCount++];
        t.pos      = k;
        t.dir      = +1;
        t.tailHold = false;
        t.headHold = false;
      }
    }
  } else {
    // ピンポン以外へ戻る際はトークン破棄（ビットパターンはsrに残っている）
    if (oldDir == DIR_PINGPONG) s_m2_tokenCount = 0;
  }

  if (newDir == DIR_RANDOM) {
    s_m2_randEnabledMask = 0;
    s_m2_randLen         = 0;
  }
}

static void m2_stepTokenPingPong(uint8_t tokenIndex, uint8_t lastIdx){
  M2Token &t = s_m2_tokens[tokenIndex];
  if (t.dir > 0) {
    if (t.tailHold) { t.tailHold = false; t.dir = -1; }
    else {
      if (t.pos < lastIdx) {
        ++t.pos;
        if (t.pos == lastIdx) t.tailHold = true;
      } else t.tailHold = true;
    }
  } else {
    if (t.headHold) { /* 削除待ち */ }
    else {
      if (t.pos > 0) {
        --t.pos;
        if (t.pos == 0) t.headHold = true;
      } else t.headHold = true;
    }
  }
}

// メインシフト処理
static void mode2_shiftWithSwitches(bool din, DirMode dirMode){
  uint8_t  idxList[16];
  uint8_t  n = 0;
  uint16_t enabledMask = 0;
  mode2_buildEnabledList(idxList, n, enabledMask);

  // 有効ステップなし：単純シフト
  if (n == 0) {
    s_m2.sr = (uint16_t)((s_m2.sr << 1) | (din ? 1u : 0u));
    s_m2_tokenCount = 0;
    if (dirMode == DIR_RANDOM && s_m2.sr == 0) {
      s_m2_randEnabledMask = 0;
      s_m2_randLen         = 0;
    }
    return;
  }

  switch (dirMode) {
    case DIR_FORWARD: {
      // idxListの順序に従ってビットをシフト
      s_m2_tokenCount = 0;
      uint8_t oldVec[16];
      uint8_t newVec[16];
      for (uint8_t k=0; k<n; ++k) oldVec[k] = (uint8_t)((s_m2.sr >> idxList[k]) & 1u);

      newVec[0] = din ? 1u : 0u;
      for (uint8_t k=1; k<n; ++k) newVec[k] = oldVec[k-1];

      uint16_t newSr = s_m2.sr;
      newSr &= (uint16_t)~enabledMask; // 有効箇所を一度クリア
      for (uint8_t k=0; k<n; ++k) {
        if (newVec[k]) newSr |= (uint16_t)(1u << idxList[k]);
      }
      s_m2.sr = newSr;
      break;
    }

    case DIR_PINGPONG: {
      uint8_t lastIdx = (uint8_t)(n - 1);
      // トークンの位置補正
      for (uint8_t i=0; i<s_m2_tokenCount; ++i) {
        if (s_m2_tokens[i].pos >= n) s_m2_tokens[i].pos = lastIdx;
      }
      // 各トークン移動
      for (uint8_t i=0; i<s_m2_tokenCount; ++i) m2_stepTokenPingPong(i, lastIdx);

      // 新規データ入力あればトークン生成
      if (din && n > 0 && s_m2_tokenCount < 16) {
        M2Token &t = s_m2_tokens[s_m2_tokenCount++];
        t.pos      = 0;
        t.dir      = +1;
        t.tailHold = false;
        t.headHold = false;
      }

      // トークン位置からSR再構築
      s_m2.sr = 0;
      for (uint8_t i=0; i<s_m2_tokenCount; ++i) {
        uint8_t lp = s_m2_tokens[i].pos;
        if (lp >= n) continue;
        s_m2.sr |= (uint16_t)(1u << idxList[lp]);
      }

      // 寿命の尽きたトークン削除
      uint8_t w = 0;
      for (uint8_t i=0; i<s_m2_tokenCount; ++i) {
        if (!s_m2_tokens[i].headHold) s_m2_tokens[w++] = s_m2_tokens[i];
      }
      s_m2_tokenCount = w;
      break;
    }

    case DIR_RANDOM:
    default: {
      s_m2_tokenCount = 0;
      if (enabledMask != s_m2_randEnabledMask || n != s_m2_randLen) {
        mode2_rebuildRandomRoute(idxList, n, enabledMask);
      }

      // ランダムルート順にシフト
      uint8_t len = s_m2_randLen;
      if (len == 0) {
        s_m2.sr = (uint16_t)((s_m2.sr << 1) | (din ? 1u : 0u));
      } else {
        uint8_t oldVec[16];
        uint8_t newVec[16];
        for (uint8_t k=0; k<len; ++k) {
          uint8_t idx = s_m2_randRoute[k];
          oldVec[k] = (uint8_t)((s_m2.sr >> idx) & 1u);
        }
        newVec[0] = din ? 1u : 0u;
        for (uint8_t k=1; k<len; ++k) newVec[k] = oldVec[k-1];

        uint16_t newSr = s_m2.sr;
        for (uint8_t k=0; k<len; ++k) {
          uint8_t idx = s_m2_randRoute[k];
          newSr &= (uint16_t)~(1u << idx);
          if (newVec[k]) newSr |= (uint16_t)(1u << idx);
        }
        s_m2.sr = newSr;
      }
      break;
    }
  }
}

static void mode2_begin() {
  s_m2 = {};
  s_debouncePushClk = Debounce{};
  trigInit(s_trigShapeA);
  trigInit(s_trigShapeB);
  seqLedSetOffAll();
  seqSetOutMask(0);

  s_m2_randEnabledMask = 0;
  s_m2_randLen         = 0;
  s_m2_tokenCount      = 0;
  s_m2_prevDir = readDirMode(seqReadSwitch(17));
}

static void mode2_update(const Inputs& in) {
  const DirMode dirMode = readDirMode(in.swDir);

  if (dirMode != s_m2_prevDir) {
    mode2_onDirChanged(s_m2_prevDir, dirMode);
    s_m2_prevDir = dirMode;
  }

  panelLedWrite(PIN_PANEL_LED_CLK, (in.lvlClk || in.pushClk) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_A,   (in.lvlAin || in.pushA) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_B,   (in.lvlBin || in.pushB) ? 100 : 0);
  panelLedWrite(PIN_PANEL_LED_C,   (in.lvlCin || in.pushC) ? 100 : 0);

  if (in.pause) {
    seqTrigA(false);
    seqTrigB(false);
    return;
  }

  // リセット
  if (in.binRise || in.pushB) {
    s_m2.sr = 0;
    s_m2_tokenCount      = 0;
    s_m2_randEnabledMask = 0;
    s_m2_randLen         = 0;
  }

  // データ計算 (IN A XOR IN C)
  bool baseData = (in.lvlAin || in.pushA);
  bool xorIn    = in.lvlCin;
  bool din      = (baseData ^ xorIn);

  // Push C (Push4) が押されている間はデータを強制0にする（データ削除）
  if (in.pushC) {
    din = false;
  }

  // シフト実行
  if (in.clkRise || in.pushClkRise) {
    mode2_shiftWithSwitches(din, dirMode);
  }

  // OUT & LED反映
  seqLedSetOffAll();
  uint16_t outMask = 0;
  for (uint8_t i=0; i<16; ++i) {
    bool bitOn = ((s_m2.sr >> i) & 1u) != 0;
    if (!bitOn) continue;

    uint8_t sw = seqReadSwitch(i+1);
    if (sw == 0) {
      // OFF: スキップ（ここにはデータが来ないはずだが念のため）
      continue;
    } else if (sw == 1) {
      // MID: データ通過のみ (LED微弱点灯)
      seqLedSetPct(i+1, 1);
    } else if (sw == 2) {
      // ON: データ出力 (LED全点灯, Gate Output)
      seqLedSetPct(i+1, 100);
      outMask |= (uint16_t)(1u << i);
    }
  }
  seqSetOutMask(outMask);

  // TRIG出力ロジック変更
  // TRIG B: DATA (XOR結果)
  // TRIG A: NOT DATA
  seqTrigB(din);
  seqTrigA(!din);
}

// =========================================================
// モード切り替え・メインループ
// =========================================================

static void enterMode(uint8_t m) {
  switch (m) {
    case 0: mode0_begin(); break;
    case 1: mode1_begin(); break;
    case 2: mode2_begin(); break;
    default: mode0_begin(); break;
  }
}

static void updateMode(const Inputs& in) {
  switch (g_mode) {
    case 0: mode0_update(in); break;
    case 1: mode1_update(in); break;
    case 2: mode2_update(in); break;
    default: mode0_update(in); break;
  }
}

void setup() {
  seqBegin();
  seqPollInputs(); // 初期状態取得
  randomSeed(analogRead(28)); // 未接続ピンで乱数シード

  enterMode(0);
  g_mode = 0;
}

void loop() {
  // PWMはハードウェアタイマーで動作中 (seqPwmServiceは空)

  // 入力ポーリング (1ms周期)
  static uint32_t t = 0;
  if (micros() - t >= 1000) {
    t = micros();
    seqPollInputs();
  }

  const Inputs in = readInputs();

  // モード変更検出
  if (in.swMode != g_mode) {
    g_mode = in.swMode;
    enterMode(g_mode);
  }

  updateMode(in);
  delayMicroseconds(100); // ループ過回転防止
}