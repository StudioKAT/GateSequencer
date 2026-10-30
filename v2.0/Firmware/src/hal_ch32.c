// ハード層（CH32V003F4U6 / ch32fun）
// ピン割り当ての正本: docs/hardware.md
#include "ch32fun.h"
#include "hal.h"
#include "seq_infilter.h"

/* ---------------------------------------------------------
 * ピン割り当て
 * --------------------------------------------------------- */
// 直結入力（トランジスタで反転 → ソフトで再反転して正論理にする）
#define PIN_IN_CLK    PD0  // EXTI0
#define PIN_IN_PAUSE  PA1  // EXTI1
#define PIN_IN_A      PD2  // EXTI2
#define PIN_IN_B      PD3  // EXTI3
#define PIN_IN_C      PD4  // EXTI4

// SPI1: 595 と 165 で SCK を共用
#define PIN_SPI_SCK   PC5
#define PIN_SPI_MOSI  PC6  // 595 SER
#define PIN_SPI_MISO  PC7  // 165 Q7
#define PIN_595_RCLK  PC1
#define PIN_165_SH_LD PC2  // LOW でパラレルロード

// TRIG 出力
#define PIN_TRIG_A    PC3
#define PIN_TRIG_B    PC4

// 乱数シード用 ADC（PA2 = A0、未接続）
#define ADC_CH_SEED   0

/* ---------------------------------------------------------
 * µs 時刻
 *   SysTick は HCLK/8 = 6MHz で回る 32bit カウンタ（約 715 秒で折り返す）。
 *   差分を積算して、uint32 で自然に折り返す µs 時刻を作る。
 *   メインループから頻繁に呼ぶ前提（715 秒以上呼ばないと時刻が飛ぶ）。
 * --------------------------------------------------------- */
static uint32_t s_lastTick;
static uint32_t s_tickRem;
static uint32_t s_micros;

uint32_t halMicros(void)
{
  const uint32_t now = SysTick->CNT;
  uint32_t d = (now - s_lastTick) + s_tickRem;
  s_lastTick = now;
  const uint32_t us = d / DELAY_US_TIME;
  s_tickRem = d - us * DELAY_US_TIME;
  s_micros += us;
  return s_micros;
}

/* ---------------------------------------------------------
 * 直結入力: レベル管理 & エッジカウンタ
 *   ISR はカウンタを増やすだけ。メイン側は前回読んだ値との差でエッジ数を判定するため、
 *   割り込み禁止区間なしで取りこぼしなく読める。
 *
 *   チャタリング対策（想定する最速クロックは 10kHz。主な使用は 0.1〜20Hz）:
 *   1. スパイク除け: レベル変化を検出したら INPUT_GLITCH_US 待って読み直し、続いているときだけ受け付ける
 *   2. 適応ロックアウト（seq_infilter）: 受け付けた後は、直前の立ち上がり間隔の 25%
 *      （最小 40µs、最大 2ms）だけ次の変化を無視する。入力ピンのヒステリシスは 150mV しかなく、
 *      ゆっくりした入力波形ではチャタリングが出るため
 *   3. ロックアウト中に無視した変化は「要確認」の印を付け、TIM2 割り込みが明けてから
 *      ピンを 2 回続けて確認して受け付ける（短いトリガの立ち下がり等を失わない）
 *   EXTI と TIM2 の両方がこの状態を触る。割り込みのネストは無効（HAL_IRQ_NESTING = 0）なので
 *   互いに割り込まない。ネストを有効にする場合はこの状態の保護が必要
 * --------------------------------------------------------- */
#define INPUT_GLITCH_US  2
#define INPUT_LOCK_MIN_US 40
#define INPUT_LOCK_MAX_US 2000

typedef struct {
  InFilter         filt;
  volatile bool    level;    // 受け付けたレベル（処理ループが読む）
  volatile uint8_t rise, fall;
  uint8_t          seenRise; // メイン側で最後に読んだ rise
} FastIn;

// 添字 = EXTI ライン番号
enum { FI_CLK = 0, FI_PAUSE, FI_A, FI_B, FI_C, FI_COUNT };
static FastIn s_fast[FI_COUNT];
static const uint32_t FAST_PIN[FI_COUNT] = { PIN_IN_CLK, PIN_IN_PAUSE, PIN_IN_A, PIN_IN_B, PIN_IN_C };
#define FAST_LINES (EXTI_Line0 | EXTI_Line1 | EXTI_Line2 | EXTI_Line3 | EXTI_Line4)
static volatile uint8_t s_needCheck; // ロックアウト中に変化を無視した入力（bit = FI_*）

// ハードウェア反転をソフトで補正して読む
static inline bool readInv(uint32_t pin) { return !funDigitalRead(pin); }

static inline void countEdge(FastIn *f, bool n)
{
  if (n) ++f->rise; else ++f->fall;
  f->level = n;
}

void EXTI7_0_IRQHandler(void) INTERRUPT_DECORATOR;
void EXTI7_0_IRQHandler(void)
{
  // フラグを先にクリアする（待ち時間中に来た次のエッジは、フラグが立ち直して再度割り込む）
  const uint32_t pending = EXTI->INTFR & FAST_LINES;
  EXTI->INTFR = pending;

  // ロックアウト中の入力は印だけ付ける。それ以外でレベルが変わった入力を候補にする
  const uint32_t now0 = SysTick->CNT;
  uint8_t candidates = 0;
  for (uint8_t i = 0; i < FI_COUNT; ++i) {
    if (!(pending & (1u << i))) continue;
    if (inFilterLocked(&s_fast[i].filt, now0)) { s_needCheck |= (uint8_t)(1u << i); continue; }
    if (readInv(FAST_PIN[i]) != s_fast[i].level) candidates |= (uint8_t)(1u << i);
  }
  if (!candidates) return;

  // 少し待って読み直し、変化が続いているものだけ受け付ける
  DelaySysTick(INPUT_GLITCH_US * DELAY_US_TIME);
  const uint32_t now = SysTick->CNT;
  for (uint8_t i = 0; i < FI_COUNT; ++i) {
    if (!(candidates & (1u << i))) continue;
    FastIn *f = &s_fast[i];
    const bool n = readInv(FAST_PIN[i]);
    if (inFilterAccept(&f->filt, n, now)) countEdge(f, n);
  }
}

// TIM2 割り込みから 20µs ごとに呼ぶ: ロックアウト明けの再確認
static void resyncFastInputs(void)
{
  if (!s_needCheck) return;
  const uint32_t now = SysTick->CNT;
  for (uint8_t i = 0; i < FI_COUNT; ++i) {
    if (!(s_needCheck & (1u << i))) continue;
    FastIn *f = &s_fast[i];
    const bool n = readInv(FAST_PIN[i]);
    if (inFilterResync(&f->filt, n, now)) {
      countEdge(f, n);
      s_needCheck &= (uint8_t)~(1u << i);
    } else if (!inFilterLocked(&f->filt, now) && n == f->level) {
      s_needCheck &= (uint8_t)~(1u << i); // 明けたときに元のレベルに戻っていた
    }
  }
}

// 前回呼び出し以降の立ち上がり回数（uint8 の差分なので 255 回までは正しく数えられる）
static uint8_t takeRise(FastIn *f)
{
  const uint8_t r = f->rise;
  const uint8_t n = (uint8_t)(r - f->seenRise);
  f->seenRise = r;
  return n;
}

void halReadFastInputs(RawInputs *raw)
{
  raw->clkRises  = takeRise(&s_fast[FI_CLK]);
  raw->aRises    = takeRise(&s_fast[FI_A]);
  raw->bRises    = takeRise(&s_fast[FI_B]);
  raw->cRises    = takeRise(&s_fast[FI_C]);
  raw->lvlClk   = s_fast[FI_CLK].level;
  raw->lvlA     = s_fast[FI_A].level;
  raw->lvlB     = s_fast[FI_B].level;
  raw->lvlC     = s_fast[FI_C].level;
  raw->lvlPause = s_fast[FI_PAUSE].level;
}

static void initFastInputs(void)
{
  funPinMode(PIN_IN_CLK,   GPIO_CFGLR_IN_FLOAT); // 外部 12kΩ で 5V にプルアップ済み
  funPinMode(PIN_IN_PAUSE, GPIO_CFGLR_IN_FLOAT);
  funPinMode(PIN_IN_A,     GPIO_CFGLR_IN_FLOAT);
  funPinMode(PIN_IN_B,     GPIO_CFGLR_IN_FLOAT);
  funPinMode(PIN_IN_C,     GPIO_CFGLR_IN_FLOAT);

  // 入力レベルとチャタリング対策の初期化
  for (uint8_t i = 0; i < FI_COUNT; ++i) {
    s_fast[i].level = readInv(FAST_PIN[i]);
    inFilterInit(&s_fast[i].filt, s_fast[i].level,
                 INPUT_LOCK_MIN_US * DELAY_US_TIME, INPUT_LOCK_MAX_US * DELAY_US_TIME);
  }

  // EXTI0..4 を両エッジで割り込み
  AFIO->EXTICR = AFIO_EXTICR_EXTI0_PD | AFIO_EXTICR_EXTI1_PA | AFIO_EXTICR_EXTI2_PD
               | AFIO_EXTICR_EXTI3_PD | AFIO_EXTICR_EXTI4_PD;
  EXTI->RTENR  |= FAST_LINES;
  EXTI->FTENR  |= FAST_LINES;
  EXTI->INTFR   = FAST_LINES;
  EXTI->INTENR |= FAST_LINES;
  NVIC_EnableIRQ(EXTI7_0_IRQn);
}

/* ---------------------------------------------------------
 * SPI（595 送信 + 165 受信）
 * --------------------------------------------------------- */
static void initSpi(void)
{
  funPinMode(PIN_595_RCLK,  GPIO_CFGLR_OUT_10Mhz_PP);
  funPinMode(PIN_165_SH_LD, GPIO_CFGLR_OUT_10Mhz_PP);
  funDigitalWrite(PIN_595_RCLK,  FUN_LOW);
  funDigitalWrite(PIN_165_SH_LD, FUN_HIGH);

  RCC->APB2PCENR |= RCC_APB2Periph_SPI1;
  funPinMode(PIN_SPI_SCK,  GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF);
  funPinMode(PIN_SPI_MOSI, GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF);
  funPinMode(PIN_SPI_MISO, GPIO_CFGLR_IN_FLOAT);

  // Mode 0（CPOL=0, CPHA=1Edge）・MSB first・48MHz/8 = 6MHz
  SPI1->CTLR1 = 0;
  SPI1->CTLR1 = SPI_CPHA_1Edge | SPI_CPOL_Low | SPI_Mode_Master | SPI_NSS_Soft
              | SPI_BaudRatePrescaler_8 | SPI_DataSize_8b | SPI_Direction_2Lines_FullDuplex;
  SPI1->CTLR1 |= CTLR1_SPE_Set;
}

static inline uint8_t spiXfer(uint8_t v)
{
  while (!(SPI1->STATR & SPI_STATR_TXE)) {}
  SPI1->DATAR = v;
  while (!(SPI1->STATR & SPI_STATR_RXNE)) {}
  return (uint8_t)SPI1->DATAR;
}

// 595 へ out595 を送り、同時に 165 から in165 を受け取る（5 バイト全二重）
static void spiTransfer(const uint8_t out595[IO_FRAME_BYTES], uint8_t in165[IO_FRAME_BYTES])
{
  // 165 パラレルロード（LOW パルス。tw は 5V で 20ns 程度あれば足りる）
  funDigitalWrite(PIN_165_SH_LD, FUN_LOW);
  __asm__ volatile("nop; nop; nop; nop;");
  funDigitalWrite(PIN_165_SH_LD, FUN_HIGH);

  // SCK 共用のため、595 送信と 165 受信を同じ 5 バイトで行う
  for (uint8_t i = 0; i < IO_FRAME_BYTES; ++i) in165[i] = spiXfer(out595[i]);
  while (SPI1->STATR & SPI_STATR_BSY) {}

  // 595 ラッチ（立ち上がりで出力に反映）
  funDigitalWrite(PIN_595_RCLK, FUN_HIGH);
  __asm__ volatile("nop; nop; nop; nop;");
  funDigitalWrite(PIN_595_RCLK, FUN_LOW);
}

/* ---------------------------------------------------------
 * BAM 送信（TIM2 の 20µs 周期割り込み）
 *   SPI の送受信はすべてこの割り込みで行う（処理ループは SPI に触らない）。
 *   区間 k（重み 2^k）は 2^k 回分の割り込みだけ続け、区間が変わるたびにその区間のフレームを送る。
 *   1 フレーム = 255 x 20µs = 5.1ms（約 195Hz、V1 と同じ）
 *   - 送るフレームは送信のたびにここで組み立てる（処理ループは出力のマスクを渡すだけ）
 *   - 処理ループが出力を更新したら、次の割り込み（最大 20µs 後）で切り替えてすぐ送る
 *   - 区間が長いとき（最長 2.56ms）も、165 を読むため 1ms ごとに送り直す
 * --------------------------------------------------------- */
#define BAM_TICK_US        20
#define BAM_RESEND_TICKS   (1000 / BAM_TICK_US) // 165 の受信間隔（1ms）

// 割り込みのネスト（入力割り込みの処理中にタイマー割り込みが割り込む）を有効にするか。
// 無効のままにすること: 入力のチャタリング対策の状態（s_fast / s_needCheck）を EXTI と TIM2 の
// 両方が触るため、ネストすると壊れる。ch32fun も V003 でのネスト（HPE）を推奨していない
#define HAL_IRQ_NESTING    0

typedef struct {
  uint16_t outMask, ledFull, ledWeak;
  uint8_t  panelMask;
} OutState;
static OutState         s_outs[2]; // 表（送信中）と裏（処理ループが書く）
static volatile uint8_t s_front;   // 表の添字
static volatile bool    s_pending; // 裏に新しい出力がある
static uint8_t          s_plane;   // 送信中の区間
static uint8_t          s_slotLeft = 1;
static uint8_t          s_sinceSend;
static volatile uint8_t s_in165[IO_FRAME_BYTES];

static void bamSend(void)
{
  const OutState *o = &s_outs[s_front];
  uint8_t tx[IO_FRAME_BYTES], rx[IO_FRAME_BYTES];
  ioBuildBamPlane(o->outMask, o->ledFull, o->ledWeak, o->panelMask, LED_DUTY_FULL, LED_DUTY_WEAK, s_plane, tx);
  spiTransfer(tx, rx);
  for (uint8_t i = 0; i < IO_FRAME_BYTES; ++i) s_in165[i] = rx[i];
  s_sinceSend = 0;
}

void TIM2_IRQHandler(void) INTERRUPT_DECORATOR;
void TIM2_IRQHandler(void)
{
  TIM2->INTFR = (uint16_t)~TIM_UIF;

  bool send = false;
  if (s_pending) {               // 処理ループの新しい出力に切り替える
    s_front  ^= 1u;
    s_pending = false;
    send = true;
  }
  if (--s_slotLeft == 0) {       // 次の区間へ
    s_plane    = (uint8_t)((s_plane + 1u) & (BAM_PLANES - 1u));
    s_slotLeft = (uint8_t)(1u << s_plane);
    send = true;
  }
  if (++s_sinceSend >= BAM_RESEND_TICKS) send = true; // 165 を読むための送り直し
  if (send) bamSend();

  resyncFastInputs(); // 直結入力のロックアウト明けの再確認
}

void halSetOutputs(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask)
{
  // 裏のバッファに書いて切り替えを予約する。割り込みが途中で切り替えないよう、書く間は割り込みを止める
  __disable_irq();
  s_outs[s_front ^ 1u] = (OutState){ outMask, ledFull, ledWeak, panelMask };
  s_pending = true;
  __enable_irq();
}

void halReadPanel(uint8_t in165[IO_FRAME_BYTES])
{
  __disable_irq();
  for (uint8_t i = 0; i < IO_FRAME_BYTES; ++i) in165[i] = s_in165[i];
  __enable_irq();
}

static void initBam(void)
{
  // 全消灯のフレームを 1 回送って 165 の初期値を得る（割り込み開始前なので直接送る）
  bamSend();

  RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
  TIM2->CTLR1 = 0;
  TIM2->PSC   = 0;                                                // 48MHz
  TIM2->ATRLR = (uint16_t)(FUNCONF_SYSTEM_CORE_CLOCK / 1000000 * BAM_TICK_US - 1); // 20µs
  TIM2->SWEVGR = 1;                                               // 設定を反映（UG）
  TIM2->INTFR = (uint16_t)~TIM_UIF;
  TIM2->DMAINTENR |= TIM_UIE;
  // タイマー（BAM）を入力割り込みより優先する（値が小さいほど優先）
  NVIC_SetPriority(TIM2_IRQn, 0x00);
  NVIC_SetPriority(EXTI7_0_IRQn, 0x80);
#if HAL_IRQ_NESTING
  __set_INTSYSCR(__get_INTSYSCR() | 0x2u);
#endif
  NVIC_EnableIRQ(TIM2_IRQn);
  TIM2->CTLR1 |= TIM_CEN;
}

/* ---------------------------------------------------------
 * TRIG 出力
 * --------------------------------------------------------- */
static void initTrig(void)
{
  funPinMode(PIN_TRIG_A, GPIO_CFGLR_OUT_10Mhz_PP);
  funPinMode(PIN_TRIG_B, GPIO_CFGLR_OUT_10Mhz_PP);
  funDigitalWrite(PIN_TRIG_A, FUN_LOW);
  funDigitalWrite(PIN_TRIG_B, FUN_LOW);
}

void halSetTrig(bool a, bool b)
{
  funDigitalWrite(PIN_TRIG_A, a ? FUN_HIGH : FUN_LOW);
  funDigitalWrite(PIN_TRIG_B, b ? FUN_HIGH : FUN_LOW);
}

/* ---------------------------------------------------------
 * 乱数シード
 * --------------------------------------------------------- */
static uint16_t adcRead(void)
{
  ADC1->CTLR2 |= ADC_SWSTART;
  while (!(ADC1->STATR & ADC_EOC)) {}
  return (uint16_t)ADC1->RDATAR;
}

uint32_t halRandomSeed(void)
{
  // ADC 初期化（PA2 をアナログ入力に）
  RCC->CFGR0 &= ~(0x1Fu << 11); // ADC クロック = HCLK/2
  RCC->APB2PCENR |= RCC_APB2Periph_ADC1;
  funPinMode(PA2, GPIO_CNF_IN_ANALOG);
  RCC->APB2PRSTR |= RCC_APB2Periph_ADC1;
  RCC->APB2PRSTR &= ~RCC_APB2Periph_ADC1;
  ADC1->RSQR1 = 0;
  ADC1->RSQR2 = 0;
  ADC1->RSQR3 = ADC_CH_SEED;
  ADC1->SAMPTR2 = (ADC1->SAMPTR2 & ~(ADC_SMP0 << (3 * ADC_CH_SEED))) | (0u << (3 * ADC_CH_SEED)); // 最短サンプル（ノイズ優先）
  ADC1->CTLR2 |= ADC_ADON | ADC_EXTSEL;
  ADC1->CTLR2 |= ADC_RSTCAL;
  while (ADC1->CTLR2 & ADC_RSTCAL) {}
  ADC1->CTLR2 |= ADC_CAL;
  while (ADC1->CTLR2 & ADC_CAL) {}

  // ADC の下位ビットと SysTick を混ぜる（FNV-1a 風）
  uint32_t h = 2166136261u;
  for (int i = 0; i < 64; ++i) {
    h ^= adcRead();
    h *= 16777619u;
    h ^= SysTick->CNT;
    h *= 16777619u;
  }
  ADC1->CTLR2 &= ~ADC_ADON; // 以後は使わない
  return h;
}

/* ---------------------------------------------------------
 * 初期化
 * --------------------------------------------------------- */
void halInit(void)
{
  funGpioInitAll(); // GPIOA/C/D と AFIO のクロック
  s_lastTick = SysTick->CNT;
  initTrig();
  initSpi();
  initFastInputs();
  initBam();
}
