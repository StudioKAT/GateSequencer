// 各モードの処理（ハード非依存）
//   0: 8step x2 シーケンサ (Dual Mode)
//   1: 16step シーケンサ (Single Mode w/ Fill & Mute)
//   2: シフトレジスタモード (Shift Register)
#pragma once
#include "seq_common.h"

// 電源投入時の状態に戻す（RAM 上の Mode 0 Sync 設定も OFF に戻る）
void seqModesPowerOn(void);

void mode0Begin(const SeqInputs *in, SeqOutputs *out);
void mode0Update(const SeqInputs *in, SeqOutputs *out);

void mode1Begin(const SeqInputs *in, SeqOutputs *out);
void mode1Update(const SeqInputs *in, SeqOutputs *out);

void mode2Begin(const SeqInputs *in, SeqOutputs *out);
void mode2Update(const SeqInputs *in, SeqOutputs *out);
