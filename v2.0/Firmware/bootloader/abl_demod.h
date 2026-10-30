// オーディオブートローダの復調（ハード非依存）
//
// 変調: 正弦波 2 値 FSK、1 ビット = 1 周期。短い周期（6kHz）= 0、長い周期（3kHz）= 1
// 入力: PAUSE 入力の立ち上がり（ジャック側の論理）の時刻。単位はティック（SysTick = HCLK = HSI 24MHz）
//
// 手順:
//   1. 待ち受け: 短い周期らしい間隔が ABL_LOCK_COUNT 回続いたら、その平均を短い周期の推定にしてロックする
//      （パケット先頭のプリアンブルは短い周期の連続）
//   2. ロック中: 短い周期と長い周期をそれぞれ推定し、その中間をしきい値にして 0 / 1 を判定する。
//      推定は受信しながら少しずつ追従させる（長い周期の推定はロック時に短い周期の 2 倍から始める）
//      - 短い周期の推定の半分より短い間隔は、ひげ・尖りとして捨てる（その立ち上がりはなかったことにする）
//      - 長い周期の推定の 1.5 倍以上は無音（パケットの間のギャップ）としてロックを外す
//   しきい値を短い周期の 1.5 倍に固定しないのは、「長い周期の直後の短い周期が伸び、短い周期の直後の
//   長い周期が縮む」偏り（実機の PAUSE 入力で実測）があるため。両方の推定の中間なら偏りに強い
// 割り算・掛け算を使わない（RV32EC には乗除算命令がない）。
#pragma once
#include <stdint.h>

// ブートローダは PLL を使わず HSI 24MHz で動かす（1920B に収めるため。Flash の待ちが 0 になるので
// CRC32 の計算は 48MHz とほぼ同じ速さ）
#define ABL_TICKS_PER_US   24u
#define ABL_SHORT_NOMINAL  (ABL_TICKS_PER_US * 1000000u / 6000u)  // 6kHz の 1 周期（ティック）
#define ABL_LONG_NOMINAL   (ABL_SHORT_NOMINAL * 2u)                // 3kHz の 1 周期（ティック）
#define ABL_SHORT_MIN      (ABL_SHORT_NOMINAL * 3u / 4u)           // 待ち受け中に短い周期とみなす範囲（公称 ±25%）
#define ABL_SHORT_MAX      (ABL_SHORT_NOMINAL * 5u / 4u)
#define ABL_LOCK_SHIFT     4u
#define ABL_LOCK_COUNT     (1u << ABL_LOCK_SHIFT)  // ロックに要る短い周期の連続回数
#define ABL_TRACK_SHIFT    4u      // 推定の追従の速さ（1/16 ずつ）

enum AblDemodEvent {
  ABL_EV_NONE = 0,  // ビットなし（待ち受け中・ひげを捨てた）
  ABL_EV_BIT0,      // 短い周期
  ABL_EV_BIT1,      // 長い周期
  ABL_EV_LOST,      // ロックが外れた（無音・範囲外の間隔）
};

typedef struct {
  uint32_t last;    // 最後に受け付けた立ち上がりの時刻
  uint32_t shortT;  // 短い周期の推定（ティック）
  uint32_t longT;   // 長い周期の推定（ティック）
  uint32_t sum;     // 待ち受け中の間隔の合計
  uint8_t  count;   // 待ち受け中に続いた短い周期の回数
  uint8_t  locked;
  uint8_t  hasLast;
} AblDemod;

// 全メンバを 0 にする。0 で初期化された静的変数は、この関数を呼んだ後と同じ状態
void ablDemodInit(AblDemod *d);
// 立ち上がり 1 回ぶん処理する。t は立ち上がりの時刻（uint32 で折り返してよい）
int  ablDemodEdge(AblDemod *d, uint32_t t);
