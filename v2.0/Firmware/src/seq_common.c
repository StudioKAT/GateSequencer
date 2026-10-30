// シーケンサ共通部（V1 GateSequencer.ino から移植）
#include "seq_common.h"

/* ---------------------------------------------------------
 * 乱数
 * --------------------------------------------------------- */
static uint32_t s_rng = 0x12345678u;

void seqRandSeed(uint32_t seed)
{
  s_rng = seed ? seed : 0x12345678u; // xorshift は 0 から抜けられない
}

static uint32_t randNext(void)
{
  s_rng ^= s_rng << 13;
  s_rng ^= s_rng >> 17;
  s_rng ^= s_rng << 5;
  return s_rng;
}

uint32_t seqRandom(uint32_t n)
{
  // CH32V003（rv32ec）には割り算命令がなく、% はソフトウェアで約 250 命令かかる。
  // n 以上の 2 のべき乗 - 1 でマスクし、範囲外なら引き直す（等確率・平均 2 回未満）
  if (n <= 1) return 0;
  uint32_t mask = n - 1;
  mask |= mask >> 1; mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16;
  uint32_t r;
  do { r = randNext() & mask; } while (r >= n);
  return r;
}

/* ---------------------------------------------------------
 * 入力スナップショット
 * --------------------------------------------------------- */

// Push_CLK 専用デバウンス管理
static struct {
  bool     rawPrev;
  bool     stable;
  uint32_t lastChangeUs;
} s_debouncePushClk;

// ボタンの立ち上がり検出用履歴
static bool s_prevPushA, s_prevPushB, s_prevPushC;

// Push A/B/C のバウンス対策: 変化はすぐ受け付け（遅れなし）、その後 PUSH_LOCKOUT_US は次の変化を無視する
// （V1 はデバウンスなしで、押下・解放のバウンスで Sync の切り替え等が複数回起こり得た）
static struct {
  bool     primed;
  bool     stable;
  uint32_t lastChangeUs;
} s_pushLock[3];

static bool lockoutPush(uint8_t i, bool raw, uint32_t now)
{
  if (!s_pushLock[i].primed) {
    s_pushLock[i].primed = true;
    s_pushLock[i].stable = raw;
    s_pushLock[i].lastChangeUs = now - PUSH_LOCKOUT_US; // 起動直後はロックアウトなしで始める
  } else if (raw != s_pushLock[i].stable && (uint32_t)(now - s_pushLock[i].lastChangeUs) >= PUSH_LOCKOUT_US) {
    s_pushLock[i].stable = raw;
    s_pushLock[i].lastChangeUs = now;
  }
  return s_pushLock[i].stable;
}

// 165 フレームの解読結果のキャッシュ。165 は 1ms ごとにしか更新されないので、
// 受信データが変わったときだけ解読し直す（処理ループの負荷の過半を占めていたため）
static struct {
  bool    valid;
  uint8_t frame[IO_FRAME_BYTES];
  uint8_t sw[16];
  uint8_t swDir, swMode;
  bool    push[4];
  uint8_t version; // 解読し直すたびに増やす
} s_dec;

static void decodePanel(const uint8_t in165[IO_FRAME_BYTES])
{
  if (s_dec.valid) {
    uint8_t i = 0;
    while (i < IO_FRAME_BYTES && s_dec.frame[i] == in165[i]) ++i;
    if (i == IO_FRAME_BYTES) return; // 変化なし
  }
  for (uint8_t i = 0; i < IO_FRAME_BYTES; ++i) s_dec.frame[i] = in165[i];
  for (uint8_t i = 0; i < 16; ++i) s_dec.sw[i] = ioDecodeSwitch(in165, (uint8_t)(i + 1));
  s_dec.swDir  = ioDecodeSwitch(in165, 17);
  s_dec.swMode = ioDecodeSwitch(in165, 18);
  for (uint8_t i = 0; i < 4; ++i) s_dec.push[i] = ioDecodePush(in165, i);
  s_dec.valid = true;
  ++s_dec.version;
}

void seqDebounceReset(void)
{
  s_debouncePushClk.rawPrev      = false;
  s_debouncePushClk.stable       = false;
  s_debouncePushClk.lastChangeUs = 0;
}

void seqInputsReset(void)
{
  seqDebounceReset();
  s_prevPushA = s_prevPushB = s_prevPushC = false;
  for (uint8_t i = 0; i < 3; ++i) s_pushLock[i].primed = false;
  s_dec.valid = false;
}

// Push_CLK デバウンス処理（立ち上がりのみ検出）
static void debouncePushClk(bool raw, uint32_t now, bool *stableOut, bool *riseOut)
{
  *riseOut = false;
  if (raw != s_debouncePushClk.rawPrev) {
    s_debouncePushClk.rawPrev = raw;
    s_debouncePushClk.lastChangeUs = now;
  }
  if ((uint32_t)(now - s_debouncePushClk.lastChangeUs) >= PUSH_DEBOUNCE_US) {
    if (s_debouncePushClk.stable != raw) {
      s_debouncePushClk.stable = raw;
      if (s_debouncePushClk.stable) *riseOut = true;
    }
  }
  *stableOut = s_debouncePushClk.stable;
}

void seqReadInputs(const RawInputs *raw, uint32_t nowUs, SeqInputs *in)
{
  seqReadInputsAt(raw, 0, nowUs, in);
}

void seqReadInputsAt(const RawInputs *raw, uint8_t edgeIndex, uint32_t nowUs, SeqInputs *in)
{
  in->nowUs = nowUs;

  // 高速入力のエッジ（edgeIndex 回目のエッジがあるか）
  in->clkRise = (raw->clkRises > edgeIndex);
  in->ainRise = (raw->aRises   > edgeIndex);
  in->binRise = (raw->bRises   > edgeIndex);
  in->cinRise = (raw->cRises   > edgeIndex);

  // レベル
  in->lvlClk = raw->lvlClk;
  in->lvlAin = raw->lvlA;
  in->lvlBin = raw->lvlB;
  in->lvlCin = raw->lvlC;
  in->pause  = raw->lvlPause;

  decodePanel(raw->in165);

  // Push_CLK（デバウンスあり）
  debouncePushClk(s_dec.push[PUSH_CLK], nowUs, &in->pushClk, &in->pushClkRise);

  // Push A/B/C（すぐ受け付け、その後 20ms はバウンスを無視）
  in->pushA = lockoutPush(0, s_dec.push[PUSH_A], nowUs);
  in->pushB = lockoutPush(1, s_dec.push[PUSH_B], nowUs);
  in->pushC = lockoutPush(2, s_dec.push[PUSH_C], nowUs);
  in->pushARise = (in->pushA && !s_prevPushA);
  in->pushBRise = (in->pushB && !s_prevPushB);
  in->pushCRise = (in->pushC && !s_prevPushC);
  s_prevPushA = in->pushA;
  s_prevPushB = in->pushB;
  s_prevPushC = in->pushC;

  // スイッチ（解読済みのキャッシュを参照する）
  in->sw        = s_dec.sw;
  in->swVersion = s_dec.version;
  in->swDir  = s_dec.swDir;
  in->swMode = s_dec.swMode;
}

/* ---------------------------------------------------------
 * ステップ進行
 * --------------------------------------------------------- */
DirMode seqReadDirMode(uint8_t sw17)
{
  return (DirMode)(sw17 > 2 ? 2 : sw17);
}

uint8_t seqNextStepLinear(uint8_t cur, DirMode mode, bool *forwardFlag, uint8_t maxSteps)
{
  switch (mode) {
    case DIR_FORWARD:
      return (uint8_t)((cur + 1) % maxSteps);

    case DIR_PINGPONG:
    default:
      if (*forwardFlag) {
        if (cur < maxSteps - 1) return (uint8_t)(cur + 1);
        *forwardFlag = false;
        return cur;
      } else {
        if (cur > 0) return (uint8_t)(cur - 1);
        *forwardFlag = true;
        return cur;
      }
  }
}

uint8_t seqNextRandom(uint8_t curStep, uint8_t maxSteps)
{
  // スイッチの状態に関わらず全ステップを候補とし、現在のステップ以外から一様に選ぶ
  // （直前と同じステップには留まらない。過去に訪れたステップは考慮しない）
  if (maxSteps <= 1) return curStep;
  const uint8_t k = (uint8_t)seqRandom((uint32_t)(maxSteps - 1));
  return (k < curStep) ? k : (uint8_t)(k + 1);
}

/* ---------------------------------------------------------
 * TRIG 最小幅シェーパ
 * --------------------------------------------------------- */
void seqTrigInit(TrigShaper *ts)
{
  ts->minUs      = TRIG_MIN_US;
  ts->bypass     = false;
  ts->prevBase   = false;
  ts->lastRiseUs = 0;
  ts->prevRiseUs = 0;
  ts->holding    = false;
}

bool seqTrigShape(bool baseLevel, TrigShaper *ts, uint32_t now)
{
  const bool baseRise = baseLevel && !ts->prevBase;
  ts->prevBase = baseLevel;

  if (baseRise) {
    ts->prevRiseUs = ts->lastRiseUs;
    ts->lastRiseUs = now;
    // 入力周期が短すぎる場合は整形を諦めてスルーする（追従性重視）
    ts->bypass = (ts->prevRiseUs != 0 && (uint32_t)(now - ts->prevRiseUs) < ts->minUs);
    ts->holding = true;
  }

  // 保持の終了は「立ち上がりからの経過時間」で判定する。期限時刻との符号付き比較
  // （V1 方式）は、立ち上がりがないまま 2^31µs（約 35.8 分）たつと符号が反転して
  // High に張り付くため使わない
  if (ts->holding && (uint32_t)(now - ts->lastRiseUs) >= ts->minUs) ts->holding = false;

  if (ts->bypass) return baseLevel;
  return baseLevel || ts->holding;
}
