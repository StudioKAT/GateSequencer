// Gate Sequencer v2 オーディオブートローダ（ブート領域 0x1FFFF000、1920B）
//
// 電源投入時にオプションバイトの設定でここから起動する。
//   - Push A と Push B を両方押していない、かつアプリが正しい（メタデータ・CRC32 が合う）→ アプリを起動
//   - それ以外 → 書き込みモード: PAUSE 入力の音声（abl_demod / abl_packet）を受けて Flash に書く
// 割り込みは使わない（ベクタテーブルを省いて 1920B に収める）。PAUSE 入力はポーリングで読む。
//
// 表示:
//   書き込み待ち: ステップ LED 1 が 1Hz で点滅
//   受信中      : ステップ LED = 進捗バー（16 段）、PAUSE LED = 信号にロックしている
//   エラー      : 全 LED が 4Hz で点滅（別製品の WAV、ページの欠け、CRC32 不一致）。受信は続ける
//   完了        : 全ステップ LED を約 2 秒点灯した後、アプリを起動
#include "ch32fun.h"
#include "abl_demod.h"
#include "abl_hal.h"
#include "abl_packet.h"
#include "io_map.h"

#define TICKS_PER_MS   (ABL_TICKS_PER_US * 1000u)
#define DONE_SHOW_MS   2000u
#define ERROR_SHOW_MS  3000u

// 0 で初期化された静的変数は、ablDemodInit / ablPacketReset / ablSessionInit を呼んだ後と同じ状態
// なので、初期化の呼び出しは省く（1920B に収めるため）
static AblDemod   s_demod;
static AblPacket  s_pkt;
static AblSession s_ss;

int main(void)
{
  SystemInit();
  ablHalInit();
  ablHalLeds(0, 0); // 595 を消灯し、165 を読む
  if (!ablHalPushAB() && ablMetaValid(ablHalFlash())) ablHalBootApp();

  ablHalFlashUnlock();

  bool     lastPin = false; // 最初の 1 回は余分なエッジになるが、復調は最初のエッジを時刻の基準にするだけ
  uint8_t  bar = 0;          // 進捗バー（割り算を避けて、書いたページごとに 16/pages ずつ進める）
  uint32_t barAcc = 0;       // pages（最大 255）+ 16 まで入るように 8bit より広く（32bit がいちばん小さい）
  uint32_t errorAt = 0;
  bool     error = false;
  uint16_t shownLeds = 0xFFFF;
  uint8_t  shownPanel = 0xFF;

  while (1) {
    const bool pin = ablHalPause();
    if (pin != lastPin) {
      lastPin = pin;
      if (pin) { // 立ち上がり（ジャック側）
        const int ev = ablDemodEdge(&s_demod, ablHalTicks());
        if (ev == ABL_EV_LOST) ablPacketReset(&s_pkt);
        if (ev == ABL_EV_BIT0 || ev == ABL_EV_BIT1) {
          const int pr = ablPacketBit(&s_pkt, ev == ABL_EV_BIT1);
          if (pr == ABL_PR_OK) {
            switch (ablSessionPacket(&s_ss, s_pkt.buf)) {
            case ABL_CMD_BEGIN:
              ablHalErasePage(ABL_META_OFFSET); // 途中で止まっても、次の起動で書き込みモードに戻る
              bar = 0; barAcc = 0;
              break;
            case ABL_CMD_WRITE_PAGE:
              ablHalWritePage((uint32_t)s_pkt.buf[1] * ABL_PAGE_SIZE, &s_pkt.buf[2]);
              for (barAcc += 16; barAcc >= s_ss.pages; barAcc -= s_ss.pages) ++bar;
              break;
            case ABL_CMD_VERIFY:
              if (ablCrc32(ablHalFlash(), ablInfoAppLen(s_ss.info)) == ablInfoAppCrc(s_ss.info)) {
                ablHalWritePage(ABL_META_OFFSET, s_pkt.buf); // END パケットの先頭 64B = メタデータ
                ablHalLeds(0xFFFF, 0);
                const uint32_t t0 = ablHalTicks();
                while ((uint32_t)(ablHalTicks() - t0) < DONE_SHOW_MS * TICKS_PER_MS) {}
                ablHalBootApp();
              }
              ablSessionRestart(&s_ss); // CRC32 が合わない: 次の再生で全ページを書き直す
              bar = 0; barAcc = 0;
              error = true; errorAt = ablHalTicks();
              break;
            case ABL_CMD_MISSING:
            case ABL_CMD_WRONG:
              error = true; errorAt = ablHalTicks();
              break;
            default:
              break;
            }
          }
        }
      }
    }

    // 表示（変わったときだけ 595 に送る。送信は約 10µs で、復調の余裕より十分短い）
    const uint32_t now = ablHalTicks();
    if (error && (uint32_t)(now - errorAt) >= ERROR_SHOW_MS * TICKS_PER_MS) error = false;
    uint16_t leds;
    uint8_t panel = s_demod.locked ? (uint8_t)(1u << PANEL_PAUSE) : 0;
    if (error) {
      const bool on = (now >> 22) & 1u; // 2^22 / 24MHz = 約 175ms ごとに反転
      leds = on ? 0xFFFF : 0;
      panel = on ? 0x7F : 0;
    } else if (s_ss.nGot == 0) {
      leds = ((now >> 24) & 1u) ? 1u : 0u; // 2^24 / 24MHz = 約 0.7 秒ごとに反転
    } else {
      leds = (uint16_t)((1u << bar) - 1u);
    }
    if (leds != shownLeds || panel != shownPanel) {
      shownLeds = leds;
      shownPanel = panel;
      ablHalLeds(leds, panel);
    }
  }
}
