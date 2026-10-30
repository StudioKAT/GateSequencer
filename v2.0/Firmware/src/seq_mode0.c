// Mode 0: 8step x2 Sequencer (A/B)
// V1 GateSequencer.ino の mode0_* を移植。スイッチは V2 仕様:
//   左 = ランダム(50%) / センター = OFF / 右 = ON
#include "seq_modes.h"

#define MAX_STEPS_MODE0 8

// Gate 出力設定（V1 の Mode A 固定）
// true: そのステップに到達したら確率に関係なく Gate OUT は HIGH（TRIG は確率依存）
static const bool kGateAlwaysOn = true;

typedef struct {
  uint8_t  step;
  bool     forward;
  bool     gateLatched;
  bool     pushPulseActive; // 手動クロック時のトリガ生成用
  uint32_t pushPulseStartUs;
} SeqState;

static SeqState s_seqA, s_seqB;
static TrigShaper s_trigShapeA, s_trigShapeB;

// Sync 設定は保存しない。電源投入時 OFF、モードを切り替えても保持する
static bool s_syncAB  = false; // Push_A: B の進行を A のクロックに同期
static bool s_syncRst = false; // Push_C: B のリセットを A と同期

void seqModesPowerOn(void)
{
  s_syncAB  = false;
  s_syncRst = false;
}

// スイッチ位置から、そのステップで TRIG を出すかを抽選する
static bool chanceFromSwitch(uint8_t pos)
{
  if (pos == SW_RIGHT) return true;
  if (pos == SW_LEFT)  return seqRandom(100) < 50;
  return false;
}

// baseIdx: Seq A = 0 (SW1..8), Seq B = 8 (SW9..16)
static bool latchGate(const SeqInputs *in, uint8_t step0, uint8_t baseIdx)
{
  return chanceFromSwitch(in->sw[baseIdx + step0]);
}

static void resetSeq(SeqState *s, const SeqInputs *in, uint8_t baseIdx)
{
  s->step = 0;
  s->forward = true;
  s->gateLatched = latchGate(in, s->step, baseIdx);
}

static void advanceSeq(SeqState *s, DirMode dir, const SeqInputs *in, uint8_t baseIdx)
{
  if (dir == DIR_RANDOM) {
    s->step = seqNextRandom(s->step, MAX_STEPS_MODE0);
  } else {
    s->step = seqNextStepLinear(s->step, dir, &s->forward, MAX_STEPS_MODE0);
  }
  s->gateLatched = latchGate(in, s->step, baseIdx);
  // 手動クロック（Push_CLK）で進んだときは、TRIG 用のパルスを PUSH_TRIG_US だけ立てる
  if (in->pushClkRise) {
    s->pushPulseActive = true;
    s->pushPulseStartUs = in->nowUs;
  }
}

// 手動クロックのパルス終了判定（経過時間で判定。ポーズが長く続いても張り付かない）
static void expirePushPulse(SeqState *s, uint32_t nowUs)
{
  if (s->pushPulseActive && (uint32_t)(nowUs - s->pushPulseStartUs) >= PUSH_TRIG_US) {
    s->pushPulseActive = false;
  }
}

void mode0Begin(const SeqInputs *in, SeqOutputs *out)
{
  s_seqA = (SeqState){0};
  s_seqB = (SeqState){0};
  resetSeq(&s_seqA, in, 0);
  resetSeq(&s_seqB, in, 8);
  seqDebounceReset();
  seqTrigInit(&s_trigShapeA);
  seqTrigInit(&s_trigShapeB);
  out->outMask = 0;
  out->ledMask = 0;
  out->ledWeakMask = 0;
}

void mode0Update(const SeqInputs *in, SeqOutputs *out)
{
  const DirMode dirMode = seqReadDirMode(in->swDir);

  // パネル LED 表示（Sync ON の間は A / C を点灯し続ける）
  out->panelClk = in->lvlClk || in->pushClk;
  out->panelA   = s_syncAB  || in->lvlAin || in->pushA;
  out->panelB   = in->lvlBin || in->pushB;
  out->panelC   = s_syncRst || in->lvlCin || in->pushC;

  // 設定切り替え（保存しない）
  if (in->pushARise) s_syncAB  = !s_syncAB;
  if (in->pushCRise) s_syncRst = !s_syncRst;

  if (in->pause) {
    out->trigA = false;
    out->trigB = false;
    return;
  }

  // リセット判定
  const bool resetA = (in->binRise || in->pushB); // Push_B or IN B
  const bool resetB = (in->cinRise || (s_syncRst && resetA));
  if (resetA) resetSeq(&s_seqA, in, 0);
  if (resetB) resetSeq(&s_seqB, in, 8);

  // クロック進行判定
  const bool clkEvent = (in->clkRise || in->pushClkRise);
  const bool advA = clkEvent;
  const bool advB = (s_syncAB ? clkEvent : in->ainRise);

  if (advA) advanceSeq(&s_seqA, dirMode, in, 0);
  if (advB) advanceSeq(&s_seqB, dirMode, in, 8);

  // gateLatched: 抽選結果（TRIG と LED に使用）
  const bool ledA = s_seqA.gateLatched;
  const bool ledB = s_seqB.gateLatched;
  // gateOut: 実際のゲート出力（Mode A なら強制 HIGH）
  const bool gateOutA = kGateAlwaysOn || ledA;
  const bool gateOutB = kGateAlwaysOn || ledB;

  uint16_t outMask = 0;
  if (gateOutA) outMask |= (uint16_t)(1u << s_seqA.step);
  if (gateOutB) outMask |= (uint16_t)(1u << (s_seqB.step + 8));
  out->outMask = outMask;

  // ステップ LED: 到達ステップは、抽選に当たれば強点灯、外れれば弱点灯（V1 と同じ）
  const uint16_t bitA = (uint16_t)(1u << s_seqA.step);
  const uint16_t bitB = (uint16_t)(1u << (s_seqB.step + 8));
  out->ledMask     = (uint16_t)((ledA ? bitA : 0) | (ledB ? bitB : 0));
  out->ledWeakMask = (uint16_t)((ledA ? 0 : bitA) | (ledB ? 0 : bitB));

  expirePushPulse(&s_seqA, in->nowUs);
  expirePushPulse(&s_seqB, in->nowUs);

  // TRIG 出力（Gate モードに関わらず抽選結果に従う）
  // A / B とも「抽選に当たったステップで、自分のクロックが HIGH か手動クロックのパルス中」に出す
  // V1 との差分: 手動クロックのパルスも抽選結果で制限し（V1 は無条件に TRIG A を出していた）、
  //             Sync AB 中は TRIG B にも手動クロックのパルスを出す（V1 は出していなかった）
  const bool clkLevelB = s_syncAB ? in->lvlClk : in->lvlAin;
  const bool baseA = s_seqA.gateLatched && (in->lvlClk || s_seqA.pushPulseActive);
  const bool baseB = s_seqB.gateLatched && (clkLevelB  || s_seqB.pushPulseActive);
  out->trigA = seqTrigShape(baseA, &s_trigShapeA, in->nowUs);
  out->trigB = seqTrigShape(baseB, &s_trigShapeB, in->nowUs);
}
