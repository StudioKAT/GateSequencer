// 直結入力（CLK / A / B / C / PAUSE）のチャタリング対策（ハード非依存の純粋関数）
//
// 適応ロックアウト: レベルの変化を受け付けたら、その後しばらく次の変化を無視する。
//   無視する長さ = 直前の立ち上がり間隔の 25%（最小 minLock、最大 maxLock）
//   - 低速（0.1〜20Hz）では最大 2ms になり、ゆっくりした入力波形のチャタリングを無視できる
//   - 高速では周期に合わせて短くなり、最速 10kHz まで追従する
//   - 立ち上がり間隔が分からないとき（起動直後）は、低速を優先して maxLock を使う
//   - テンポが一瞬で 4 倍を超えて速くなると、間隔を学び直すまでの数発を取りこぼすことがある
//
// ロックアウト中に無視した変化は、明けてからピンを 2 回続けて確認し（inFilterResync）、
// レベルが違っていれば受け付ける。短いトリガの立ち下がりなどを失わないため。
// 時刻の単位は呼び出し側が決める（ハード層は SysTick のカウント値、テストは µs）。
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t minLock, maxLock; // ロックアウト長の範囲
  bool     level;            // 受け付けたレベル
  bool     hasRise;
  uint32_t lastRise;         // 最後に受け付けた立ち上がりの時刻
  uint32_t period;           // 直前の立ち上がり間隔（0 = 不明）
  uint32_t lockStart;        // 最後に変化を受け付けた時刻
  uint32_t lockLen;          // そのときのロックアウト長
  bool     resyncSeen;       // ロックアウト明けにレベル違いを 1 回見た
} InFilter;

void inFilterInit(InFilter *f, bool level, uint32_t minLock, uint32_t maxLock);
bool inFilterLocked(const InFilter *f, uint32_t now);
// 変化を受け付ける。ロックアウト中か、レベルが同じなら false（何もしない）
bool inFilterAccept(InFilter *f, bool level, uint32_t now);
// ロックアウト明けの再確認（定期的に呼ぶ）。2 回続けてレベル違いを見たら受け付けて true
bool inFilterResync(InFilter *f, bool pinLevel, uint32_t now);
