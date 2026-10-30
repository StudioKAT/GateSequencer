// V2 基板の 595 / 165 チェーンのビット配置とデコード
#include "io_map.h"

// ビット位置は (バイト番号 * 8 + ビット番号)
#define BP(byte, bit) ((uint8_t)((byte) * 8 + (bit)))

/* ---------------------------------------------------------
 * 595 チェーン: MOSI → U3 → U5 → U8 → U10 → U12
 *   byte0 = U12, byte1 = U10, byte2 = U8, byte3 = U5, byte4 = U3
 *   bit n = Q(A+n)
 *
 *   U3/U5/U8/U10 の 1 個（byte 4-j、j = 0..3）は、ch 2j+1, 2j+2 と ch 2j+9, 2j+10 を受け持つ
 *     OUT: ch 2j+1 = QC, ch 2j+2 = QE, ch 2j+9 = QG, ch 2j+10 = QH
 *     LED: ch 2j+1 = QB, ch 2j+2 = QD, ch 2j+9 = QF, ch 2j+10 = QA
 *   （例: j=0 → U3。OUT1=U3.QC, OUT2=U3.QE, OUT9=U3.QG, OUT10=U3.QH）
 *   U12（byte0）の QB..QH = パネル LED A..G、QA は未使用
 *
 *   処理ループで毎周呼ぶため、ch 2 個分（2bit）ごとの 4 要素表で 1 バイトを組み立てる
 * --------------------------------------------------------- */
#define Q(n) ((uint8_t)(1u << (n)))
static const uint8_t OUT_LO[4] = { 0, Q(2), Q(4), Q(2) | Q(4) };  // ch 2j+1, 2j+2
static const uint8_t OUT_HI[4] = { 0, Q(6), Q(7), Q(6) | Q(7) };  // ch 2j+9, 2j+10
static const uint8_t LED_LO[4] = { 0, Q(1), Q(3), Q(1) | Q(3) };
static const uint8_t LED_HI[4] = { 0, Q(5), Q(0), Q(5) | Q(0) };

// 上の LED_LO / LED_HI と同じ配置を 1 個ずつ並べたもの（j = 0..3 → byte 4-j）
const uint8_t IO_STEP_LED_BIT[16] = {
  BP(4,1), BP(4,3), BP(3,1), BP(3,3), BP(2,1), BP(2,3), BP(1,1), BP(1,3), // LED 1..8
  BP(4,5), BP(4,0), BP(3,5), BP(3,0), BP(2,5), BP(2,0), BP(1,5), BP(1,0), // LED 9..16
};

/* ---------------------------------------------------------
 * 165 チェーン: U2 → U4 → U7 → U9 → U11 → MISO
 *   byte0 = U11, byte1 = U9, byte2 = U7, byte3 = U4, byte4 = U2
 *   bit n = Dn
 * --------------------------------------------------------- */
static const uint8_t SWA_BIT[18] = {
  BP(4,4), BP(4,3), BP(3,4), BP(3,3), BP(2,4), BP(2,3), BP(1,4), BP(1,3),
  BP(4,6), BP(4,1), BP(3,6), BP(3,1), BP(2,6), BP(2,1), BP(1,6), BP(1,1),
  BP(0,3), BP(0,1)
};
static const uint8_t SWB_BIT[18] = {
  BP(4,5), BP(4,2), BP(3,5), BP(3,2), BP(2,5), BP(2,2), BP(1,5), BP(1,2),
  BP(4,7), BP(4,0), BP(3,7), BP(3,0), BP(2,7), BP(2,0), BP(1,7), BP(1,0),
  BP(0,2), BP(0,0)
};
// CLK, A, B, C
static const uint8_t PUSH_BIT[4] = { BP(0,7), BP(0,6), BP(0,5), BP(0,4) };

static inline bool getBit(const uint8_t in[IO_FRAME_BYTES], uint8_t pos)
{
  return ((in[pos / 8] >> (pos % 8)) & 1u) != 0;
}

void ioBuildOutFrame(uint16_t outMask, uint16_t ledMask, uint8_t panelMask,
                     uint8_t frame[IO_FRAME_BYTES])
{
  // シフト量を定数にするため 4 個ぶんを展開して書く（j = 0..3 → frame[4 - j]）
#define IO_BYTE(j) ((uint8_t)(OUT_LO[(outMask >> (2 * (j))) & 3u] | OUT_HI[(outMask >> (2 * (j) + 8)) & 3u] \
                            | LED_LO[(ledMask >> (2 * (j))) & 3u] | LED_HI[(ledMask >> (2 * (j) + 8)) & 3u]))
  frame[0] = ioPanelFrameByte(panelMask);
  frame[4] = IO_BYTE(0);
  frame[3] = IO_BYTE(1);
  frame[2] = IO_BYTE(2);
  frame[1] = IO_BYTE(3);
#undef IO_BYTE
}

void ioBuildBamPlane(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask,
                     uint8_t fullDuty, uint8_t weakDuty, uint8_t k,
                     uint8_t frame[IO_FRAME_BYTES])
{
  uint16_t led = 0;
  if ((fullDuty >> k) & 1u) led |= ledFull;
  if ((weakDuty >> k) & 1u) led |= (uint16_t)(ledWeak & ~ledFull); // 重なりは強点灯を優先
  ioBuildOutFrame(outMask, led, panelMask, frame);
}

void ioBuildBamPlanes(uint16_t outMask, uint16_t ledFull, uint16_t ledWeak, uint8_t panelMask,
                      uint8_t fullDuty, uint8_t weakDuty,
                      uint8_t planes[BAM_PLANES][IO_FRAME_BYTES])
{
  for (uint8_t k = 0; k < BAM_PLANES; ++k) {
    ioBuildBamPlane(outMask, ledFull, ledWeak, panelMask, fullDuty, weakDuty, k, planes[k]);
  }
}

uint8_t ioDecodeSwitch(const uint8_t in[IO_FRAME_BYTES], uint8_t sw)
{
  if (sw < 1 || sw > 18) return SW_CENTER;
  const bool aHigh = getBit(in, SWA_BIT[sw - 1]);
  const bool bHigh = getBit(in, SWB_BIT[sw - 1]);
  // V2 は V1 と接点が逆: 左で B(2-3)、右で A(1-2) が GND に落ちる
  if ( aHigh && !bHigh) return SW_LEFT;
  if (!aHigh &&  bHigh) return SW_RIGHT;
  return SW_CENTER; // 両方 HIGH = センター、両方 LOW（異常）もセンター扱い
}

bool ioDecodePush(const uint8_t in[IO_FRAME_BYTES], uint8_t idx)
{
  if (idx > PUSH_C) return false;
  return !getBit(in, PUSH_BIT[idx]); // プルアップ・LOW アクティブ
}
