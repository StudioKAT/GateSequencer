// SequencerIO.cpp
#include <Arduino.h>
#include "SequencerIO.h"
#include <SPI.h>

extern "C" {
  #include "pico/time.h"   // repeating_timer用
}

/* ---------------------------------------------------------
 * ピン割り当て定義
 * --------------------------------------------------------- */

// 74HC595 (SPI1使用) - 出力拡張用
static const uint8_t PIN_595_RCLK = 9;
static const uint8_t PIN_595_SCK  = 10;
static const uint8_t PIN_595_MOSI = 11;

// 74HC165 (bit-bang制御) - 入力拡張用
static const uint8_t PIN_165_Q7    = 0;  // シリアルデータ入力 (IC5末端から)
static const uint8_t PIN_165_SH_LD = 1;  // Shift/Load (LOWでロード)
static const uint8_t PIN_165_CLK   = 2;  // クロック

// 直結入力ピン（ハードウェア反転回路あり → ソフトで再反転して正論理にする）
static const uint8_t PIN_IN_CLK   = 3;
static const uint8_t PIN_IN_A     = 4;
static const uint8_t PIN_IN_B     = 5;
static const uint8_t PIN_IN_C     = 6;
static const uint8_t PIN_IN_PAUSE = 7;

// パネルLED（直結・PWM・正論理）
// ヘッダでextern宣言された実体をここで定義
const uint8_t PIN_PANEL_LED_CLK = 26; // 旧 LED_A
const uint8_t PIN_PANEL_LED_A   = 15; // 旧 LED_B
const uint8_t PIN_PANEL_LED_B   = 14; // 旧 LED_C
const uint8_t PIN_PANEL_LED_C   = 13; // 旧 LED_D

// トリガ出力ピン
static const uint8_t PIN_TRIG_A = 8;
static const uint8_t PIN_TRIG_B = 12;

/* ---------------------------------------------------------
 * 74HC595 マップ（OUT / LED）
 * --------------------------------------------------------- */
static uint8_t s_595Data[4] = {0, 0, 0, 0};
static const bool REVERSE_BYTE_ORDER = false;

// OUTジャックへのビットマッピング (ch1〜16)
static const int OUT_BIT_MAP[16] = {
  (0*8+5),(0*8+0),(1*8+5),(1*8+0),
  (2*8+5),(2*8+0),(3*8+5),(3*8+0),
  (0*8+7),(0*8+6),(1*8+7),(1*8+6),
  (2*8+7),(2*8+6),(3*8+7),(3*8+6)
};

// ステップLEDへのビットマッピング (ch1〜16)
static const int LED_BIT_MAP[16] = {
  (0*8+2),(0*8+4),(1*8+2),(1*8+4),
  (2*8+2),(2*8+4),(3*8+2),(3*8+4),
  (0*8+1),(0*8+3),(1*8+1),(1*8+3),
  (2*8+1),(2*8+3),(3*8+1),(3*8+3)
};

// 内部バッファの特定ビットを操作
static inline void _595_setBit(int bitpos, bool on){
  if (bitpos < 0) return;
  uint8_t b = (uint8_t)(bitpos / 8);
  uint8_t m = (uint8_t)(1u << (bitpos % 8));
  if (on)  s_595Data[b] |=  m;
  else     s_595Data[b] &= (uint8_t)~m;
}

// SPIでデータ送信
static void _595_flush(){
  SPI1.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  if (!REVERSE_BYTE_ORDER) {
    for (int i=0; i<4; ++i) SPI1.transfer(s_595Data[i]);
  } else {
    for (int i=3; i>=0; --i) SPI1.transfer(s_595Data[i]);
  }
  SPI1.endTransaction();

  // ラッチ信号生成
  digitalWrite(PIN_595_RCLK, HIGH);
  asm volatile("nop; nop; nop;"); // 短いwait
  digitalWrite(PIN_595_RCLK, LOW);
}

static void _595_zero(){
  s_595Data[0] = s_595Data[1] = s_595Data[2] = s_595Data[3] = 0;
}

/* ---------------------------------------------------------
 * 74HC165（40bit ストリーム読み込み）
 * --------------------------------------------------------- */
static uint8_t s_165Data[5] = {0, 0, 0, 0, 0};

static void _165_read40_stream(){
  // パラレルロード (非同期)
  digitalWrite(PIN_165_SH_LD, LOW);
  delayMicroseconds(1);
  digitalWrite(PIN_165_SH_LD, HIGH);

  for (int i=0; i<5; ++i) s_165Data[i] = 0;

  // IC5 → IC1 の順で40bitシフトイン
  for (int bit=0; bit<40; ++bit){
    uint8_t b = (uint8_t)(digitalRead(PIN_165_Q7) & 1u);
    int grp = bit / 8;
    int pos = bit % 8;
    s_165Data[grp] |= (uint8_t)(b << (7 - pos)); // MSB First的に格納

    digitalWrite(PIN_165_CLK, HIGH);
    digitalWrite(PIN_165_CLK, LOW);
  }
}

static inline bool _165_getBit(int byteIndex, int d){
  return ((s_165Data[byteIndex] >> d) & 1u) != 0;
}

/* ---------------------------------------------------------
 * SW / PUSH ビットマップ定義
 * --------------------------------------------------------- */
// スイッチA接点 (3ポジションの下側検出用など)
static const uint8_t SWA_BIT[18] = {
  /*1*/  (4*8+4), /*2*/  (4*8+3), /*3*/  (3*8+4), /*4*/  (3*8+3),
  /*5*/  (2*8+4), /*6*/  (2*8+3), /*7*/  (1*8+4), /*8*/  (1*8+3),
  /*9*/  (4*8+6), /*10*/ (4*8+1), /*11*/ (3*8+6), /*12*/ (3*8+1),
  /*13*/ (2*8+6), /*14*/ (2*8+1), /*15*/ (1*8+6), /*16*/ (1*8+1),
  /*17*/ (0*8+2), /*18*/ (0*8+0)
};
// スイッチB接点
static const uint8_t SWB_BIT[18] = {
  /*1*/  (4*8+5), /*2*/  (4*8+2), /*3*/  (3*8+5), /*4*/  (3*8+2),
  /*5*/  (2*8+5), /*6*/  (2*8+2), /*7*/  (1*8+5), /*8*/  (1*8+2),
  /*9*/  (4*8+7), /*10*/ (4*8+0), /*11*/ (3*8+7), /*12*/ (3*8+0),
  /*13*/ (2*8+7), /*14*/ (2*8+0), /*15*/ (1*8+7), /*16*/ (1*8+0),
  /*17*/ (0*8+3), /*18*/ (0*8+1)
};
// Pushボタン (1=CLK, 2=A, 3=B, 4=C)
static const uint8_t PUSH_BIT[4] = { (0*8+4),(0*8+5),(0*8+6),(0*8+7) };

/* ---------------------------------------------------------
 * 高速ゲート入力：レベル管理 & エッジカウンタ
 * --------------------------------------------------------- */
static volatile bool s_lvlClk   = false;
static volatile bool s_lvlA     = false;
static volatile bool s_lvlB     = false;
static volatile bool s_lvlC     = false;
static volatile bool s_lvlPause = false;

static volatile uint32_t s_riseClk=0, s_fallClk=0;
static volatile uint32_t s_riseA=0,   s_fallA=0;
static volatile uint32_t s_riseB=0,   s_fallB=0;
static volatile uint32_t s_riseC=0,   s_fallC=0;
static volatile uint32_t s_riseP=0,   s_fallP=0;

// ハードウェア反転をソフトで補正して読み取る
static inline bool _readInv(uint8_t pin){ return !digitalRead(pin); }

// 割り込みサービスルーチン (ISR)
static void _isr_clk(){
  bool n = _readInv(PIN_IN_CLK);
  if (n != s_lvlClk){
    if (n) ++s_riseClk; else ++s_fallClk;
    s_lvlClk = n;
  }
}
static void _isr_a(){
  bool n = _readInv(PIN_IN_A);
  if (n != s_lvlA){
    if (n) ++s_riseA; else ++s_fallA;
    s_lvlA = n;
  }
}
static void _isr_b(){
  bool n = _readInv(PIN_IN_B);
  if (n != s_lvlB){
    if (n) ++s_riseB; else ++s_fallB;
    s_lvlB = n;
  }
}
static void _isr_c(){
  bool n = _readInv(PIN_IN_C);
  if (n != s_lvlC){
    if (n) ++s_riseC; else ++s_fallC;
    s_lvlC = n;
  }
}
static void _isr_pause(){
  bool n = _readInv(PIN_IN_PAUSE);
  if (n != s_lvlPause){
    if (n) ++s_riseP; else ++s_fallP;
    s_lvlPause = n;
  }
}

/* ---------------------------------------------------------
 * OUT / LED 状態管理（PWM制御）
 * --------------------------------------------------------- */
static volatile uint16_t g_outMask    = 0;      // OUT 16bit マスク
static volatile uint8_t  g_ledPct[16] = {0};    // 各chのLED輝度 (0..100)

static uint8_t g_pwmPhase = 0;
static repeating_timer_t g_pwmTimer;

// PWM更新周期: 約20µs * 256ステップ ≒ 5.1ms周期 (約195Hz)
static const int32_t PWM_TICK_US = 20;

// %指定をPWMデューティ(0-255)に変換
// 1%指定時は、ごく暗く点灯させるためにDuty1を返す特別処理を入れる
static inline uint8_t dutyFromPct(uint8_t pct){
  if (pct == 0)   return 0;
  if (pct == 1)   return 1;        // 最小点灯
  if (pct >= 100) return 255;

  uint16_t v = (uint16_t)pct * 255u + 50u; // 四捨五入計算
  v /= 100u;
  if (v > 255u) v = 255u;
  return (uint8_t)v;
}

// PWM処理の本体：タイマー割り込みから呼ばれる
static inline void _flushOutAndLed(uint16_t outMask, uint8_t phase){
  _595_zero();

  // OUT ビット設定 (ONなら常時1)
  for (uint8_t ch=0; ch<16; ++ch){
    if ((outMask >> ch) & 1u){
      _595_setBit(OUT_BIT_MAP[ch], true);
    }
  }

  // LED PWM処理 (phase < duty の区間だけON)
  for (uint8_t ch=0; ch<16; ++ch){
    uint8_t pct  = g_ledPct[ch];
    uint8_t duty = dutyFromPct(pct);
    if (phase < duty){
      _595_setBit(LED_BIT_MAP[ch], true);
    }
  }

  _595_flush();
}

// タイマーコールバック
static bool _pwmTimerCallback(repeating_timer_t *t){
  (void)t;
  g_pwmPhase++; // オーバーフローで0に戻る動作を利用
  _flushOutAndLed(g_outMask, g_pwmPhase);
  return true;  // タイマー継続
}

/* ---------------------------------------------------------
 * 公開 API 実装
 * --------------------------------------------------------- */
void seqBegin(){
  // 74HC595 (Output) Setup
  pinMode(PIN_595_RCLK, OUTPUT); digitalWrite(PIN_595_RCLK, LOW);
  SPI1.setTX(PIN_595_MOSI);
  SPI1.setSCK(PIN_595_SCK);
  SPI1.begin();
  _595_zero();
  _595_flush();

  // 74HC165 (Input) Setup
  pinMode(PIN_165_SH_LD, OUTPUT); digitalWrite(PIN_165_SH_LD, HIGH);
  pinMode(PIN_165_CLK,   OUTPUT); digitalWrite(PIN_165_CLK,   LOW);
  pinMode(PIN_165_Q7,    INPUT);

  // パネルLED Setup
  pinMode(PIN_PANEL_LED_CLK, OUTPUT);
  pinMode(PIN_PANEL_LED_A,   OUTPUT);
  pinMode(PIN_PANEL_LED_B,   OUTPUT);
  pinMode(PIN_PANEL_LED_C,   OUTPUT);
  digitalWrite(PIN_PANEL_LED_CLK, LOW);
  digitalWrite(PIN_PANEL_LED_A,   LOW);
  digitalWrite(PIN_PANEL_LED_B,   LOW);
  digitalWrite(PIN_PANEL_LED_C,   LOW);

  // 直結入力 Setup
  pinMode(PIN_IN_CLK,   INPUT);
  pinMode(PIN_IN_A,     INPUT);
  pinMode(PIN_IN_B,     INPUT);
  pinMode(PIN_IN_C,     INPUT);
  pinMode(PIN_IN_PAUSE, INPUT);

  // TRIG Output Setup
  pinMode(PIN_TRIG_A, OUTPUT);
  pinMode(PIN_TRIG_B, OUTPUT);
  digitalWrite(PIN_TRIG_A, LOW);
  digitalWrite(PIN_TRIG_B, LOW);

  // 入力レベルの初期化
  s_lvlClk   = _readInv(PIN_IN_CLK);
  s_lvlA     = _readInv(PIN_IN_A);
  s_lvlB     = _readInv(PIN_IN_B);
  s_lvlC     = _readInv(PIN_IN_C);
  s_lvlPause = _readInv(PIN_IN_PAUSE);

  // 割り込み設定
  attachInterrupt(digitalPinToInterrupt(PIN_IN_CLK),   _isr_clk,   CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_A),     _isr_a,     CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_B),     _isr_b,     CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_C),     _isr_c,     CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_IN_PAUSE), _isr_pause, CHANGE);

  // 内部状態初期化
  g_outMask   = 0;
  for (int i=0; i<16; ++i) g_ledPct[i] = 0;
  g_pwmPhase  = 0;

  _flushOutAndLed(g_outMask, g_pwmPhase);

  // PWMハードウェアタイマー開始
  add_repeating_timer_us(PWM_TICK_US, _pwmTimerCallback, nullptr, &g_pwmTimer);
}

void seqPollInputs(){
  _165_read40_stream();
}

void seqPwmService(){
  // ハードウェアタイマー化に伴い、空実装
}

/* ---- Switch / Push ---- */
uint8_t seqReadSwitch(uint8_t idx1to18){
  if (idx1to18 < 1 || idx1to18 > 18) return 1;
  uint8_t ai = SWA_BIT[idx1to18-1];
  uint8_t bi = SWB_BIT[idx1to18-1];
  bool aHigh = _165_getBit(ai/8, ai%8);
  bool bHigh = _165_getBit(bi/8, bi%8);
  // 回路仕様に基づくデコード
  // !A && B -> 0 (下)
  // A && B  -> 1 (中)
  // A && !B -> 2 (上)
  if (!aHigh &&  bHigh) return 0;
  if ( aHigh &&  bHigh) return 1;
  if ( aHigh && !bHigh) return 2;
  return 1; // default center
}

bool seqReadPush(uint8_t idx1to4){
  if (idx1to4 < 1 || idx1to4 > 4) return false;
  uint8_t bi = PUSH_BIT[idx1to4-1];
  bool high = _165_getBit(bi/8, bi%8);
  return !high; // プルアップ・Lowアクティブと想定し、押下でtrueを返す
}

/* ---- Gate Inputs (Levels) ---- */
bool seqReadClkIn()   { return s_lvlClk;   }
bool seqReadAIn()     { return s_lvlA;     }
bool seqReadBIn()     { return s_lvlB;     }
bool seqReadCIn()     { return s_lvlC;     }
bool seqReadPauseIn() { return s_lvlPause; }

/* ---- Gate Inputs (Edge Counts) ---- */
// カウンタを読み出して0クリアするアトミック操作ヘルパー
static inline uint32_t _takeAndClear(volatile uint32_t& v){
  uint32_t n = v;
  v = 0;
  return n;
}
uint32_t seqTakeAndClearClkInRise(){   return _takeAndClear(s_riseClk); }
uint32_t seqTakeAndClearClkInFall(){   return _takeAndClear(s_fallClk); }
uint32_t seqTakeAndClearAInRise(){     return _takeAndClear(s_riseA);   }
uint32_t seqTakeAndClearAInFall(){     return _takeAndClear(s_fallA);   }
uint32_t seqTakeAndClearBInRise(){     return _takeAndClear(s_riseB);   }
uint32_t seqTakeAndClearBInFall(){     return _takeAndClear(s_fallB);   }
uint32_t seqTakeAndClearCInRise(){     return _takeAndClear(s_riseC);   }
uint32_t seqTakeAndClearCInFall(){     return _takeAndClear(s_fallC);   }
uint32_t seqTakeAndClearPauseRise(){   return _takeAndClear(s_riseP);   }
uint32_t seqTakeAndClearPauseFall(){   return _takeAndClear(s_fallP);   }

/* ---- OUT Output ---- */
void seqSetOutMask(uint16_t mask){
  g_outMask = mask;
}

void seqGateWrite(uint8_t ch, bool level){
  if (ch < 1 || ch > 16) return;
  uint16_t m = (uint16_t)(1u << (ch-1));
  if (level) g_outMask |=  m;
  else       g_outMask &= ~m;
}

void seqGateWriteMask(uint16_t mask){
  g_outMask = mask;
}

void seqAllGatesOff(){
  g_outMask = 0;
}

void seqTrigA(bool level){
  digitalWrite(PIN_TRIG_A, level ? HIGH : LOW);
}
void seqTrigB(bool level){
  digitalWrite(PIN_TRIG_B, level ? HIGH : LOW);
}

/* ---- Step LEDs ---- */
void seqLedSetPct(uint8_t ch, uint8_t pct){
  if (ch < 1 || ch > 16) return;
  if (pct > 100) pct = 100;
  g_ledPct[ch-1] = pct;
}

void seqLedSetOffAll(){
  for (int i=0; i<16; ++i) g_ledPct[i] = 0;
}