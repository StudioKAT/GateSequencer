// Mode 1: 16step Sequencer (FILL / MUTE)
// V1 GateSequencer.ino の mode1_* を移植。スイッチは V2 仕様:
//   左 = TRIG A / センター = OFF（TRIG なし）/ 右 = TRIG B
//   FILL 中は全ステップを右（TRIG B）、MUTE 中は全ステップをセンター（ゲートも LOW）として扱う
#include "seq_modes.h"

#define MAX_STEPS_MODE1 16

// ステップの TRIG 振り分け
typedef enum { ROUTE_NONE = 0, ROUTE_A, ROUTE_B } Route;

static struct {
  uint8_t step;
  bool    forward;
  Route   route; // ステップ到達時にスイッチから確定する
} s_m1;

static TrigShaper s_trigShapeA, s_trigShapeB;

static Route latchRoute(const SeqInputs *in, uint8_t step)
{
  switch (in->sw[step]) {
    case SW_LEFT:  return ROUTE_A;
    case SW_RIGHT: return ROUTE_B;
    default:       return ROUTE_NONE;
  }
}

static void resetStep(const SeqInputs *in)
{
  s_m1.step    = 0;
  s_m1.forward = true;
  s_m1.route   = latchRoute(in, s_m1.step);
}

void mode1Begin(const SeqInputs *in, SeqOutputs *out)
{
  resetStep(in);
  seqDebounceReset();
  seqTrigInit(&s_trigShapeA);
  seqTrigInit(&s_trigShapeB);
  out->outMask = 0;
  out->ledMask = 0;
  out->ledWeakMask = 0;
}

void mode1Update(const SeqInputs *in, SeqOutputs *out)
{
  const DirMode dirMode = seqReadDirMode(in->swDir);

  out->panelClk = in->lvlClk || in->pushClk;
  out->panelA   = in->lvlAin || in->pushA;
  out->panelB   = in->lvlBin || in->pushB;
  out->panelC   = in->lvlCin || in->pushC;

  const bool clkLevel   = (in->lvlClk || in->pushClk);
  const bool fillActive = (in->lvlAin || in->pushA); // FILL 機能
  const bool muteActive = (in->lvlCin || in->pushC); // MUTE 機能

  if (in->pause) {
    out->trigA = false;
    out->trigB = false;
    return;
  }

  // リセット
  if (in->binRise || in->pushB) resetStep(in);

  // クロック
  if (in->clkRise || in->pushClkRise) {
    if (dirMode == DIR_RANDOM) {
      s_m1.step = seqNextRandom(s_m1.step, MAX_STEPS_MODE1);
    } else {
      s_m1.step = seqNextStepLinear(s_m1.step, dirMode, &s_m1.forward, MAX_STEPS_MODE1);
    }
    s_m1.route = latchRoute(in, s_m1.step);
  }

  // FILL / MUTE による上書き（MUTE が優先）
  Route route = s_m1.route;
  bool  gateOut = true; // Mode A: 到達ステップで常に HIGH
  if (fillActive) route = ROUTE_B;
  if (muteActive) { route = ROUTE_NONE; gateOut = false; }

  out->outMask = gateOut ? (uint16_t)(1u << s_m1.step) : 0;

  // ステップ LED: 到達ステップは、TRIG が出れば強点灯、出なければ弱点灯（V1 と同じ。MUTE 中も弱点灯）
  const uint16_t stepBit = (uint16_t)(1u << s_m1.step);
  out->ledMask     = (route != ROUTE_NONE) ? stepBit : 0;
  out->ledWeakMask = (route != ROUTE_NONE) ? 0 : stepBit;

  out->trigA = seqTrigShape(route == ROUTE_A && clkLevel, &s_trigShapeA, in->nowUs);
  out->trigB = seqTrigShape(route == ROUTE_B && clkLevel, &s_trigShapeB, in->nowUs);
}
