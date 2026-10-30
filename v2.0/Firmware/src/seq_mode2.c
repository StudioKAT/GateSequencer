// Mode 2: Shift Register (Variable Length & Routing)
// V1 GateSequencer.ino の mode2_* を移植。スイッチは V2 仕様:
//   センター = SKIP（経路から除外）/ 左 = MUTE（データは通すが出力しない）/ 右 = ACTIVE（通して出力）
#include "seq_modes.h"

static uint16_t s_sr; // 16bit シフトレジスタ実体

// PingPong モード用トークン管理
// V1 はトークンを配列で 1 個ずつ動かしていた。ここでは「有効ビット列上の位置」を bit にした
// 3 つのマスクで持ち、全トークンをビット演算でまとめて動かす（処理ループの負荷対策）。
//   fwd : 末尾へ進む途中のトークン
//   tail: 末尾に着いて 1 ステップ滞留中のトークン（次のステップで back になる）
//   back: 先頭へ戻る途中のトークン（先頭に着いたステップで出力してから消える）
// 同じ位置・同じ状態のトークンは以後まったく同じに動くので、1 個にまとめても出力は変わらない
static struct { uint16_t fwd, tail, back; } s_tok;

static inline void clearTokens(void) { s_tok.fwd = s_tok.tail = s_tok.back = 0; }

static uint8_t popcount16(uint16_t v)
{
  uint8_t c = 0;
  while (v) { v &= (uint16_t)(v - 1); ++c; }
  return c;
}

// ランダムルーティング管理
static uint16_t s_randEnabledMask;
static uint8_t  s_randRoute[16];
static uint8_t  s_randLen;

static DirMode s_prevDir;
static TrigShaper s_trigShapeA, s_trigShapeB;

// スイッチ由来の情報のキャッシュ（スイッチが変わったときだけ作り直す）
static struct {
  bool     valid;
  uint8_t  version;     // SeqInputs.swVersion
  uint8_t  idxList[16]; // 経路に含めるステップ（MUTE / ACTIVE）を先頭から並べたもの
  uint8_t  n;
  uint16_t enabledMask; // MUTE / ACTIVE
  uint16_t activeMask;  // ACTIVE（出力するステップ）
  uint16_t tapMask;     // TRIG A/B に出す位置（経路の最後のステップ。全 SKIP なら index 15）
} s_swc;

static void refreshSwitchCache(const SeqInputs *in)
{
  if (s_swc.valid && s_swc.version == in->swVersion) return;
  s_swc.n = 0;
  s_swc.enabledMask = 0;
  s_swc.activeMask  = 0;
  for (uint8_t i = 0; i < 16; ++i) {
    const uint8_t pos = in->sw[i];
    if (pos == SW_CENTER) continue; // SKIP は経路から除外
    s_swc.idxList[s_swc.n++] = i;
    s_swc.enabledMask |= (uint16_t)(1u << i);
    if (pos == SW_RIGHT) s_swc.activeMask |= (uint16_t)(1u << i);
  }
  // Forward の末尾・Random の固定された終点・PingPong の折り返し点はどれも最後の有効ステップ
  s_swc.tapMask = (uint16_t)(1u << (s_swc.n ? s_swc.idxList[s_swc.n - 1] : 15));
  s_swc.version = in->swVersion;
  s_swc.valid   = true;
}

// ランダムルーティング再構築（端点は固定、中間のみシャッフル）
static void rebuildRandomRoute(const uint8_t *idxList, uint8_t n, uint16_t enabledMask)
{
  s_randLen         = n;
  s_randEnabledMask = enabledMask;

  if (n == 0) return;
  if (n == 1) { s_randRoute[0] = idxList[0]; return; }
  if (n == 2) { s_randRoute[0] = idxList[0]; s_randRoute[1] = idxList[1]; return; }

  s_randRoute[0]     = idxList[0];
  s_randRoute[n - 1] = idxList[n - 1];

  uint8_t tmp[16];
  const uint8_t m = (uint8_t)(n - 2);
  for (uint8_t i = 0; i < m; ++i) tmp[i] = idxList[i + 1];

  // Fisher-Yates shuffle
  for (int k = (int)m - 1; k > 0; --k) {
    const int j = (int)seqRandom((uint32_t)(k + 1));
    const uint8_t t = tmp[k];
    tmp[k] = tmp[j];
    tmp[j] = t;
  }
  for (uint8_t i = 0; i < m; ++i) s_randRoute[1 + i] = tmp[i];
}

// 方向切り替え時の状態引継ぎ
static void onDirChanged(const SeqInputs *in, DirMode oldDir, DirMode newDir)
{
  if (oldDir == newDir) return;

  if (newDir == DIR_PINGPONG) {
    // 直線→ピンポン：現在 ON のビットをトークン化して動きを与える
    refreshSwitchCache(in);
    const uint8_t *idxList = s_swc.idxList;
    const uint8_t  n       = s_swc.n;

    clearTokens();
    for (uint8_t k = 0; k < n; ++k) {
      if ((s_sr >> idxList[k]) & 1u) s_tok.fwd |= (uint16_t)(1u << k);
    }
  } else {
    // ピンポン以外へ戻る際はトークン破棄（ビットパターンは sr に残っている）
    if (oldDir == DIR_PINGPONG) clearTokens();
  }

  if (newDir == DIR_RANDOM) {
    s_randEnabledMask = 0;
    s_randLen         = 0;
  }
}

// route[0] → route[1] → … の順にビットを 1 つずらし、route[0] に din を入れる
// mask は route に含まれる位置全体（それ以外のビットはそのまま残す）
static uint16_t shiftAlong(uint16_t sr, const uint8_t *route, uint8_t len, uint16_t mask, bool din)
{
  uint16_t newSr = (uint16_t)(sr & ~mask);
  if (din) newSr |= (uint16_t)(1u << route[0]);
  for (uint8_t k = 1; k < len; ++k) {
    if ((sr >> route[k - 1]) & 1u) newSr |= (uint16_t)(1u << route[k]);
  }
  return newSr;
}

// メインシフト処理
static void shiftWithSwitches(const SeqInputs *in, bool din, DirMode dirMode)
{
  refreshSwitchCache(in);
  const uint8_t *idxList     = s_swc.idxList;
  const uint8_t  n           = s_swc.n;
  const uint16_t enabledMask = s_swc.enabledMask;

  // 有効ステップなし：単純シフト
  if (n == 0) {
    s_sr = (uint16_t)((s_sr << 1) | (din ? 1u : 0u));
    clearTokens();
    if (dirMode == DIR_RANDOM && s_sr == 0) {
      s_randEnabledMask = 0;
      s_randLen         = 0;
    }
    return;
  }

  switch (dirMode) {
    case DIR_FORWARD:
      // idxList の順序に従ってビットをシフト
      clearTokens();
      s_sr = shiftAlong(s_sr, idxList, n, enabledMask, din);
      break;

    case DIR_PINGPONG: {
      const uint8_t  lastIdx = (uint8_t)(n - 1);
      const uint16_t lastBit = (uint16_t)(1u << lastIdx);
      const uint16_t inRange = (uint16_t)((n >= 16) ? 0xFFFFu : ((1u << n) - 1u));

      // V1 と同じく、移動前のトークン数で新規追加の可否を決める
      const bool canAdd = (popcount16(s_tok.fwd) + popcount16(s_tok.tail) + popcount16(s_tok.back)) < 16;

      // トークンの位置補正（有効ステップが減って範囲外になったものは末尾へ）
      uint16_t fwd = s_tok.fwd, tail = s_tok.tail, back = s_tok.back;
      if (fwd  & ~inRange) fwd  = (uint16_t)((fwd  & inRange) | lastBit);
      if (tail & ~inRange) tail = (uint16_t)((tail & inRange) | lastBit);
      if (back & ~inRange) back = (uint16_t)((back & inRange) | lastBit);

      // 移動
      //   tail → その場で back に変わる
      //   fwd  → 1 つ進む。末尾に着いた（または末尾にいた）ものは tail になる
      //   back → 1 つ戻る。先頭に着いた（または先頭にいた）ものは、このステップで出力してから消える
      const uint16_t fwdMoved  = (uint16_t)((fwd & ~lastBit) << 1);
      const uint16_t newTail   = (uint16_t)((fwd & lastBit) | (fwdMoved & lastBit));
      uint16_t       newFwd    = (uint16_t)(fwdMoved & ~lastBit);
      const uint16_t backMoved = (uint16_t)((back & ~1u) >> 1);
      const uint16_t atHead    = (uint16_t)((back & 1u) | (backMoved & 1u));
      const uint16_t newBack   = (uint16_t)(tail | (backMoved & ~1u));

      // 新規データ入力あれば先頭にトークン生成
      if (din && canAdd) newFwd |= 1u;

      // トークン位置から SR 再構築（先頭で消えるトークンもこのステップでは出力する）
      uint16_t occupied = (uint16_t)(newFwd | newTail | newBack | atHead);
      uint16_t sr = 0;
      for (uint8_t k = 0; occupied; ++k, occupied >>= 1) {
        if (occupied & 1u) sr |= (uint16_t)(1u << idxList[k]);
      }
      s_sr = sr;
      s_tok.fwd = newFwd; s_tok.tail = newTail; s_tok.back = newBack;
      break;
    }

    case DIR_RANDOM:
    default:
      clearTokens();
      if (enabledMask != s_randEnabledMask || n != s_randLen) {
        rebuildRandomRoute(idxList, n, enabledMask);
      }
      // ランダムルート順にシフト（ルートは有効ステップ全体の並べ替えなので、マスクは enabledMask）
      s_sr = shiftAlong(s_sr, s_randRoute, s_randLen, enabledMask, din);
      break;
  }
}

void mode2Begin(const SeqInputs *in, SeqOutputs *out)
{
  s_sr = 0;
  seqDebounceReset();
  seqTrigInit(&s_trigShapeA);
  seqTrigInit(&s_trigShapeB);
  out->outMask = 0;
  out->ledMask = 0;
  out->ledWeakMask = 0;

  s_randEnabledMask = 0;
  s_randLen         = 0;
  clearTokens();
  s_swc.valid       = false;
  s_prevDir = seqReadDirMode(in->swDir);
}

void mode2Update(const SeqInputs *in, SeqOutputs *out)
{
  const DirMode dirMode = seqReadDirMode(in->swDir);

  if (dirMode != s_prevDir) {
    onDirChanged(in, s_prevDir, dirMode);
    s_prevDir = dirMode;
  }

  out->panelClk = in->lvlClk || in->pushClk;
  out->panelA   = in->lvlAin || in->pushA;
  out->panelB   = in->lvlBin || in->pushB;
  out->panelC   = in->lvlCin || in->pushC;

  if (in->pause) {
    out->trigA = false;
    out->trigB = false;
    return;
  }

  // リセット
  if (in->binRise || in->pushB) {
    s_sr = 0;
    clearTokens();
    s_randEnabledMask = 0;
    s_randLen         = 0;
  }

  // データ計算 (IN A XOR IN C)
  const bool baseData = (in->lvlAin || in->pushA);
  const bool xorIn    = in->lvlCin;
  bool din = (baseData ^ xorIn);

  // Push_C が押されている間はデータを強制 0 にする（データ削除）
  if (in->pushC) din = false;

  // シフト実行
  if (in->clkRise || in->pushClkRise) shiftWithSwitches(in, din, dirMode);

  // OUT & LED 反映: ACTIVE のビットは出力して強点灯、MUTE のビットは出力せず弱点灯（V1 と同じ）
  refreshSwitchCache(in);
  const uint16_t outMask = (uint16_t)(s_sr & s_swc.activeMask);
  out->outMask = outMask;
  out->ledMask = outMask;
  out->ledWeakMask = (uint16_t)(s_sr & s_swc.enabledMask & ~s_swc.activeMask);

  // TRIG B: 経路の最後のステップのビット / TRIG A: その反転（V1 は入力の XOR をそのまま出していた）
  // TRIG B を IN C にパッチすると、有効ステップ数を周期にパターンがループする
  const bool tap = (s_sr & s_swc.tapMask) != 0;
  out->trigB = tap;
  out->trigA = !tap;
}
