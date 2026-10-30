// オーディオブートローダのパケット解析と受信ページ管理（ハード非依存）。仕様は abl_packet.h
// ブート領域（1920B）に収めるため、CRC32 は表を使わずビットごとに計算する
#include "abl_packet.h"

uint32_t ablRd32(const uint8_t *p) { return (uint32_t)ablRd16(p) | ((uint32_t)ablRd16(p + 2) << 16); }

uint32_t ablCrc32(const uint8_t *p, uint32_t n)
{
  uint32_t crc = 0xFFFFFFFFu;
  while (n--) {
    crc ^= *p++;
    for (uint8_t i = 0; i < 8; ++i) crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
  }
  return ~crc;
}

bool ablMetaValid(const uint8_t *flash)
{
  const uint8_t *m = flash + ABL_META_OFFSET;
  const uint8_t *info = m + ABL_META_INFO;
  const uint16_t len = ablInfoAppLen(info);
  return m[0] == ABL_PKT_END && ablInfoProduct(info) == ABL_PRODUCT_ID
      && len != 0 && len <= ABL_APP_MAX && ablCrc32(flash, len) == ablInfoAppCrc(info);
}

/* ---------------------------------------------------------
 * パケットの組み立て
 * --------------------------------------------------------- */
void ablPacketReset(AblPacket *pk)
{
  pk->sr = 0;
  pk->inBody = 0;
}

int ablPacketBit(AblPacket *pk, uint8_t bit)
{
  if (!pk->inBody) {
    pk->sr = (uint16_t)((pk->sr >> 1) | (bit ? 0x8000u : 0u)); // LSB から届く
    if (pk->sr == ABL_SYNC) {
      pk->inBody = 1;
      pk->bit = 0;
      pk->len = 0;
    }
    return ABL_PR_NONE;
  }
  pk->cur = (uint8_t)((pk->cur >> 1) | (bit ? 0x80u : 0u));
  if (++pk->bit < 8) return ABL_PR_NONE;
  pk->bit = 0;
  pk->buf[pk->len] = pk->cur;
  if (++pk->len < ABL_PKT_LEN) return ABL_PR_NONE;

  ablPacketReset(pk);
  const uint8_t n = ABL_PKT_LEN - ABL_CRC_SIZE;
  return ablCrc32(pk->buf, n) == ablRd32(&pk->buf[n]) ? ABL_PR_OK : ABL_PR_BAD;
}

/* ---------------------------------------------------------
 * 受信ページ管理
 * --------------------------------------------------------- */
void ablSessionInit(AblSession *ss)
{
  ss->started = 0;
  ablSessionRestart(ss);
}

void ablSessionRestart(AblSession *ss)
{
  ss->nGot = 0;
  for (uint8_t i = 0; i < sizeof ss->got; ++i) ss->got[i] = 0;
}

uint8_t ablSessionPacket(AblSession *ss, const uint8_t *pkt)
{
  const uint8_t type = pkt[0];

  if (type == ABL_PKT_DATA) {
    const uint8_t page = pkt[1];
    if (!ss->started || page >= ss->pages) return ABL_CMD_NONE; // 範囲外は書かない（メタデータページ・ブート領域を守る）
    const uint8_t mask = (uint8_t)(1u << (page & 7u));
    if (ss->got[page >> 3] & mask) return ABL_CMD_NONE;
    ss->got[page >> 3] |= mask;
    ++ss->nGot;
    return ABL_CMD_WRITE_PAGE;
  }

  if (type != ABL_PKT_START && type != ABL_PKT_END) return ABL_CMD_NONE;
  const uint8_t *in = &pkt[1];
  const uint16_t len = ablInfoAppLen(in);
  if (ablInfoProduct(in) != ABL_PRODUCT_ID || len == 0 || len > ABL_APP_MAX) return ABL_CMD_WRONG;
  // 比較とコピーを 1 回のループで行う（同じならコピーしても変わらない）
  uint8_t diff = (uint8_t)!ss->started;
  for (uint8_t i = 0; i < ABL_INFO_SIZE; ++i) {
    diff |= (uint8_t)(ss->info[i] ^ in[i]);
    ss->info[i] = in[i];
  }
  if (diff) {
    ss->started = 1;
    ss->pages = (uint8_t)((len + ABL_PAGE_SIZE - 1u) >> 6);
    ablSessionRestart(ss);
    return ABL_CMD_BEGIN; // 別の更新の END から始まった場合も、まずは BEGIN（ページは次の再生で受ける）
  }
  if (type != ABL_PKT_END) return ABL_CMD_NONE;
  return ss->nGot == ss->pages ? ABL_CMD_VERIFY : ABL_CMD_MISSING;
}
