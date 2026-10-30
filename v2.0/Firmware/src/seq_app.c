// アプリ層: モード切替・パネル LED・595 フレーム生成
#include "seq_app.h"
#include "seq_modes.h"

static uint8_t    s_mode;
static SeqOutputs s_seqOut; // 前回値を保持する（V1 の 595 バッファ相当）

// 前回ハード層へ渡した 595 の出力
static bool     s_outValid;
static uint16_t s_lastOut, s_lastLed, s_lastWeak;
static uint8_t  s_lastPanel;

static void enterMode(uint8_t m, const SeqInputs *in)
{
  switch (m) {
    case 1:  mode1Begin(in, &s_seqOut); break;
    case 2:  mode2Begin(in, &s_seqOut); break;
    default: mode0Begin(in, &s_seqOut); break;
  }
}

static void updateMode(const SeqInputs *in)
{
  switch (s_mode) {
    case 1:  mode1Update(in, &s_seqOut); break;
    case 2:  mode2Update(in, &s_seqOut); break;
    default: mode0Update(in, &s_seqOut); break;
  }
}

void appInit(const RawInputs *raw, uint32_t nowUs, uint32_t seed)
{
  seqRandSeed(seed);
  seqInputsReset();
  seqModesPowerOn();
  s_seqOut = (SeqOutputs){0};
  s_outValid = false;

  // 通常起動（設定保存・コンフィグモードは V2 初期実装では持たない）
  SeqInputs in;
  seqReadInputs(raw, nowUs, &in);
  s_mode = 0;
  enterMode(s_mode, &in);
}

static uint8_t maxU8(uint8_t a, uint8_t b) { return a > b ? a : b; }

// edgeIndex 回目のエッジ 1 回分の処理
static void stepOnce(const RawInputs *raw, uint8_t edgeIndex, uint32_t nowUs, SeqInputs *in)
{
  seqReadInputsAt(raw, edgeIndex, nowUs, in);

  // モード変更検出
  if (in->swMode != s_mode) {
    s_mode = in->swMode;
    enterMode(s_mode, in);
  }

  updateMode(in);
}

void appUpdate(const RawInputs *raw, uint32_t nowUs, AppOutputs *out)
{
  // 1 サイクルの間に同じ入力のエッジが複数回来ていたら、その回数分だけ処理する
  // （高速クロックでメインループ 1 周より短い間隔でエッジが来てもステップを取りこぼさない）
  const uint8_t n = maxU8(maxU8(raw->clkRises, raw->aRises), maxU8(raw->bRises, raw->cRises));
  SeqInputs in;
  uint8_t i = 0;
  do {
    stepOnce(raw, i, nowUs, &in);
  } while (++i < n);

  uint8_t panel = 0;
  if (s_seqOut.panelClk) panel |= 1u << PANEL_CLK;
  if (s_seqOut.panelA)   panel |= 1u << PANEL_A;
  if (s_seqOut.panelB)   panel |= 1u << PANEL_B;
  if (s_seqOut.panelC)   panel |= 1u << PANEL_C;
  // V2 で追加したパネル LED（視覚確認用）
  if (in.pause)          panel |= 1u << PANEL_PAUSE;
  if (s_seqOut.trigA)    panel |= 1u << PANEL_OUT_A;
  if (s_seqOut.trigB)    panel |= 1u << PANEL_OUT_B;

  out->outMask   = s_seqOut.outMask;
  out->ledMask   = s_seqOut.ledMask;
  out->ledWeakMask = s_seqOut.ledWeakMask;
  out->panelMask = panel;
  out->trigA     = s_seqOut.trigA;
  out->trigB     = s_seqOut.trigB;
  // 595 の出力が変わったか（変わった周だけハード層へ渡す）
  out->outputsChanged = !s_outValid || out->outMask != s_lastOut || out->ledMask != s_lastLed
                     || out->ledWeakMask != s_lastWeak || panel != s_lastPanel;
  if (out->outputsChanged) {
    s_lastOut = out->outMask; s_lastLed = out->ledMask; s_lastWeak = out->ledWeakMask; s_lastPanel = panel;
    s_outValid = true;
  }
}

uint8_t appMode(void)
{
  return s_mode;
}
