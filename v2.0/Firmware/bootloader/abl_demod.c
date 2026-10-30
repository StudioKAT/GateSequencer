// オーディオブートローダの復調（ハード非依存）。仕様は abl_demod.h
#include "abl_demod.h"

// すべて 0 にする（周期の推定はロックしたときに決まるので、それまでは使わない）
void ablDemodInit(AblDemod *d)
{
  d->last = 0;
  d->shortT = 0;
  d->longT = 0;
  d->sum = 0;
  d->count = 0;
  d->locked = 0;
  d->hasLast = 0;
}

static int hunt(AblDemod *d, uint32_t p)
{
  if (p < ABL_SHORT_MIN || p > ABL_SHORT_MAX) {
    d->sum = 0;
    d->count = 0;
    return ABL_EV_NONE;
  }
  d->sum += p;
  if (++d->count == ABL_LOCK_COUNT) {
    d->shortT = d->sum >> ABL_LOCK_SHIFT;
    d->longT = d->shortT << 1;
    d->locked = 1;
  }
  return ABL_EV_NONE;
}

// 推定 *est を p に 1/16 ずつ寄せる
static void track(uint32_t *est, uint32_t p)
{
  const int32_t diff = (int32_t)(p - *est);
  *est = (uint32_t)((int32_t)*est + (diff >> ABL_TRACK_SHIFT));
}

int ablDemodEdge(AblDemod *d, uint32_t t)
{
  if (!d->hasLast) {
    d->hasLast = 1;
    d->last = t;
    return ABL_EV_NONE;
  }
  const uint32_t p = t - d->last;

  if (!d->locked) {
    d->last = t;
    return hunt(d, p);
  }

  if (p < (d->shortT >> 1)) return ABL_EV_NONE; // ひげ・尖り: この立ち上がりは捨てる
  d->last = t;
  if (p < ((d->shortT + d->longT) >> 1)) {
    track(&d->shortT, p);
    return ABL_EV_BIT0;
  }
  if (p < d->longT + (d->longT >> 1)) {
    track(&d->longT, p);
    return ABL_EV_BIT1;
  }
  // 無音のあと: ロックを外し、この立ち上がりから待ち受けをやり直す
  d->locked = 0;
  d->sum = 0;
  d->count = 0;
  return ABL_EV_LOST;
}
