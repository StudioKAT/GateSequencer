// 直結入力のチャタリング対策（適応ロックアウト）
#include "seq_infilter.h"

void inFilterInit(InFilter *f, bool level, uint32_t minLock, uint32_t maxLock)
{
  f->minLock    = minLock;
  f->maxLock    = maxLock;
  f->level      = level;
  f->hasRise    = false;
  f->lastRise   = 0;
  f->period     = 0;
  f->lockStart  = 0;
  f->lockLen    = 0;
  f->resyncSeen = false;
}

bool inFilterLocked(const InFilter *f, uint32_t now)
{
  return (uint32_t)(now - f->lockStart) < f->lockLen;
}

bool inFilterAccept(InFilter *f, bool level, uint32_t now)
{
  if (level == f->level || inFilterLocked(f, now)) return false;

  if (level) { // 立ち上がり: 間隔を記録
    f->period   = f->hasRise ? (uint32_t)(now - f->lastRise) : 0;
    f->lastRise = now;
    f->hasRise  = true;
  }
  uint32_t lock = f->period ? (f->period >> 2) : f->maxLock; // 間隔の 25%（不明なら最大）
  if (lock < f->minLock) lock = f->minLock;
  if (lock > f->maxLock) lock = f->maxLock;

  f->level      = level;
  f->lockStart  = now;
  f->lockLen    = lock;
  f->resyncSeen = false;
  return true;
}

bool inFilterResync(InFilter *f, bool pinLevel, uint32_t now)
{
  if (inFilterLocked(f, now) || pinLevel == f->level) {
    f->resyncSeen = false;
    return false;
  }
  if (!f->resyncSeen) { // 1 回目は様子見（ロックアウト明けのスパイクを拾わない）
    f->resyncSeen = true;
    return false;
  }
  return inFilterAccept(f, pinLevel, now);
}
