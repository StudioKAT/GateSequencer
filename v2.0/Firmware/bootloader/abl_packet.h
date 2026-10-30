// オーディオブートローダのパケット解析と受信ページ管理（ハード非依存）
//
// ビット列（abl_demod の出力）の形式。バイトは LSB から送る。多バイトの値はリトルエンディアン
//   プリアンブル（0 の連続）+ 同期語 ABL_SYNC（16bit）+ 種別（1B）+ 本体（65B）+ CRC32（種別と本体に対して）
//   パケットの長さは種別によらず ABL_PKT_LEN（70B）で固定（長さの分岐をなくして小さくするため）
//   - ABL_PKT_START / ABL_PKT_END: 本体 = 更新の情報（10B、下記）+ 0 埋め。更新の始まりと終わり
//                                  （エンコーダは各周の前に START を送る）
//   - ABL_PKT_DATA:                本体 = ページ番号（1B）+ 64B
// 更新の情報（10B）: 製品 ID（2B）、バージョン（2B）、アプリのバイト数（2B）、アプリの CRC32（4B）
// CRC32 は IEEE（Python の zlib.crc32）。パケットとアプリ全体の検証の両方に使う
// （ブート領域 1920B に収めるため、CRC は 1 種類にし、情報は生のバイト列のまま扱う）
//
// 受信ページ管理（AblSession）はパケットを受け取り、ハード層がすべき操作（AblCmd）を返す。
// Flash の操作そのものはハード層が行う。
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define ABL_SYNC          0x2DD4u
#define ABL_PRODUCT_ID    0x4753u  // "GS"（Gate Sequencer）
#define ABL_PAGE_SIZE     64u
#define ABL_FLASH_SIZE    16384u
#define ABL_APP_MAX       (ABL_FLASH_SIZE - ABL_PAGE_SIZE)     // 最後のページはメタデータ
#define ABL_APP_PAGES     (ABL_APP_MAX / ABL_PAGE_SIZE)       // 255
#define ABL_META_OFFSET   ABL_APP_MAX                          // メタデータページの Flash 先頭からの位置

enum AblPktType { ABL_PKT_START = 1, ABL_PKT_DATA = 2, ABL_PKT_END = 3 };

#define ABL_INFO_SIZE     10u
#define ABL_CRC_SIZE      4u
#define ABL_PKT_LEN       (1u + 1u + ABL_PAGE_SIZE + ABL_CRC_SIZE) // 70B

// メタデータページ = 検証が通った END パケットの先頭 64B をそのまま書いたもの
//   [0] = ABL_PKT_END（有効の印。消去後の 0xFF なら無効）、[1..10] = 更新の情報
// （専用の組み立て処理を持たないことで、ブートローダを小さくする）
#define ABL_META_INFO     1u
static inline uint16_t ablRd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t ablRd32(const uint8_t *p) __attribute__((noinline)); // 複数のファイルから使うので 1 か所に置く（サイズ）
static inline uint16_t ablInfoProduct(const uint8_t *info) { return ablRd16(info); }
static inline uint16_t ablInfoAppLen(const uint8_t *info)  { return ablRd16(info + 4); }
static inline uint32_t ablInfoAppCrc(const uint8_t *info)  { return ablRd32(info + 6); }

uint32_t ablCrc32(const uint8_t *p, uint32_t n);
// Flash の内容（先頭から ABL_FLASH_SIZE バイト）で、メタデータが正しくアプリの CRC32 が合うか
bool     ablMetaValid(const uint8_t *flash);

/* ---------------------------------------------------------
 * パケットの組み立て（ビット → パケット）
 * --------------------------------------------------------- */
enum AblPktResult { ABL_PR_NONE = 0, ABL_PR_OK, ABL_PR_BAD };

typedef struct {
  uint16_t sr;       // 同期語を探すシフトレジスタ
  uint8_t  inBody;
  uint8_t  bit;      // 組み立て中のバイトのビット位置
  uint8_t  cur;      // 組み立て中のバイト
  uint8_t  len;      // 受信済みのバイト数
  uint8_t  buf[ABL_PKT_LEN];
} AblPacket;

void ablPacketReset(AblPacket *pk) __attribute__((noinline)); // 複数箇所から呼ぶので展開させない（サイズ）
// 1 ビット処理する。パケットが揃ったら ABL_PR_OK（CRC 一致）か ABL_PR_BAD（不一致）を返す。
// ABL_PR_OK のとき pk->buf[0] が種別、pk->buf[1..] が本体（知らない種別でも CRC が合えば OK を返す）
int  ablPacketBit(AblPacket *pk, uint8_t bit);

/* ---------------------------------------------------------
 * 受信ページ管理（パケット → ハード層への指示）
 * --------------------------------------------------------- */
enum AblCmd {
  ABL_CMD_NONE = 0,
  ABL_CMD_BEGIN,       // 新しい更新が始まった: メタデータページを消す
  ABL_CMD_WRITE_PAGE,  // pkt[1] 番目のページに pkt[2..65]（64B）を書く
  ABL_CMD_VERIFY,      // 全ページ揃った: アプリ appLen バイトの CRC32 を確かめ、合えばメタデータを書いて完了
  ABL_CMD_MISSING,     // END を受けたがページが欠けている（もう一度再生してもらう）
  ABL_CMD_WRONG,       // 別の製品の WAV、または不正な長さ（何も消さない）
};

typedef struct {
  uint8_t info[ABL_INFO_SIZE]; // 受信中の更新の情報
  uint8_t started;
  uint8_t pages;     // このアプリのページ数
  uint8_t nGot;      // 書いたページ数（進捗表示用）
  uint8_t got[(ABL_APP_PAGES + 7u) / 8u];
} AblSession;

// 0 で初期化された静的変数は、ablSessionInit・ablPacketReset を呼んだ後と同じ状態
void ablSessionInit(AblSession *ss);
// ABL_PR_OK のパケット（pk->buf）を処理し、ハード層への指示（AblCmd）を返す
uint8_t ablSessionPacket(AblSession *ss, const uint8_t *pkt);
// VERIFY の CRC32 が合わなかったとき: 受信済みの印を消し、次の再生で全ページを書き直す
void ablSessionRestart(AblSession *ss) __attribute__((noinline)); // 複数箇所から呼ぶので展開させない（サイズ）
