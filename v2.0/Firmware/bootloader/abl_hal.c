// オーディオブートローダのハード層（CH32V003 / ch32fun）
// ピンの正本: docs/hardware.md。595 / 165 の手順は src/hal_ch32.c と同じ
#include "ch32fun.h"
#include "abl_hal.h"
#include "io_map.h"

// PAUSE 入力 = PA1（トランジスタで反転）、SPI1 SCK / MOSI / MISO = PC5 / PC6 / PC7
#define PIN_595_RCLK  PC1
#define PIN_165_SH_LD PC2

#define FLASH_BASE_ADDR 0x08000000u

static uint8_t s_in165[IO_FRAME_BYTES];

// GPIOC の設定（1 ピン 4bit、PC7..PC0）。1920B に収めるため funPinMode を使わず一度に書く
//   PC7 MISO 入力 / PC6 MOSI・PC5 SCK 代替機能 50MHz / PC4・PC3（TRIG）・PC0 入力（リセット後と同じ）
//   PC2 SH/LD・PC1 RCLK 出力 10MHz
#define CFG_IN   0x4u
#define CFG_OUT  (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)
#define CFG_AF   (GPIO_Speed_50MHz | GPIO_CNF_OUT_PP_AF)
#define GPIOC_CFG ((CFG_IN << 28) | (CFG_AF << 24) | (CFG_AF << 20) | (CFG_IN << 16) \
                 | (CFG_IN << 12) | (CFG_OUT << 8) | (CFG_OUT << 4) | CFG_IN)

void ablHalInit(void)
{
  // PA1（PAUSE 入力）はリセット後の入力（フローティング）のまま使う。外部 12kΩ で 5V にプルアップ済み
  RCC->APB2PCENR |= RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOC | RCC_APB2Periph_SPI1;
  GPIOC->BSHR = (1u << 2) | (1u << (16 + 1)); // SH/LD = HIGH、RCLK = LOW から始める
  GPIOC->CFGLR = GPIOC_CFG;
  SPI1->CTLR1 = SPI_CPHA_1Edge | SPI_CPOL_Low | SPI_Mode_Master | SPI_NSS_Soft
              | SPI_BaudRatePrescaler_8 | SPI_DataSize_8b | SPI_Direction_2Lines_FullDuplex;
  SPI1->CTLR1 |= CTLR1_SPE_Set;
}

void ablHalLeds(uint16_t leds, uint8_t panel)
{
  // ioBuildOutFrame は表が大きいので、ステップ LED のビット位置の表で 1 個ずつ立てる（ゲート出力は常に LOW）
  uint8_t out[IO_FRAME_BYTES] = { ioPanelFrameByte(panel), 0, 0, 0, 0 };
  for (uint8_t ch = 0; ch < 16; ++ch) {
    const uint8_t pos = IO_STEP_LED_BIT[ch];
    if (leds & (1u << ch)) out[pos >> 3] |= (uint8_t)(1u << (pos & 7u));
  }
  // 165 のロード・595 のラッチのパルスは、GPIO への 2 回の書き込みの間隔（24MHz で 1 クロック以上、約 42ns）で
  // 足りる（74HC165 / 595 の要求は 5V で約 20ns）。本体の hal_ch32.c にある nop は 1920B に収めるため省く
  funDigitalWrite(PIN_165_SH_LD, FUN_LOW);
  funDigitalWrite(PIN_165_SH_LD, FUN_HIGH);
  for (uint8_t i = 0; i < IO_FRAME_BYTES; ++i) {
    while (!(SPI1->STATR & SPI_STATR_TXE)) {}
    SPI1->DATAR = out[i];
    while (!(SPI1->STATR & SPI_STATR_RXNE)) {}
    s_in165[i] = (uint8_t)SPI1->DATAR;
  }
  while (SPI1->STATR & SPI_STATR_BSY) {}
  funDigitalWrite(PIN_595_RCLK, FUN_HIGH);
  funDigitalWrite(PIN_595_RCLK, FUN_LOW);
}

bool ablHalPushAB(void)
{
  return ioDecodePush(s_in165, PUSH_A) && ioDecodePush(s_in165, PUSH_B);
}

uint32_t ablHalTicks(void) { return SysTick->CNT; }

bool ablHalPause(void) { return !(GPIOA->INDR & (1u << 1)); } // PA1、トランジスタの反転を戻す

/* ---------------------------------------------------------
 * Flash
 * --------------------------------------------------------- */

const uint8_t *ablHalFlash(void) { return (const uint8_t *)FLASH_BASE_ADDR; }

void ablHalFlashUnlock(void)
{
  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
  FLASH->MODEKEYR = FLASH_KEY1; // 64B ページの高速消去・書き込み
  FLASH->MODEKEYR = FLASH_KEY2;
}

static void waitBusy(void) { while (FLASH->STATR & FLASH_STATR_BSY) {} }

void ablHalErasePage(uint32_t offset)
{
  FLASH->CTLR = CR_PAGE_ER;
  FLASH->ADDR = FLASH_BASE_ADDR + offset;
  FLASH->CTLR = CR_STRT_Set | CR_PAGE_ER;
  waitBusy();
}

// 消去してから 64B を書く（ch32fun examples/flashtest と同じ手順）
void ablHalWritePage(uint32_t offset, const uint8_t *data)
{
  ablHalErasePage(offset);
  volatile uint32_t *p = (volatile uint32_t *)(FLASH_BASE_ADDR + offset);
  FLASH->CTLR = CR_PAGE_PG;
  FLASH->CTLR = CR_BUF_RST | CR_PAGE_PG;
  FLASH->ADDR = FLASH_BASE_ADDR + offset;
  waitBusy();
  for (uint8_t i = 0; i < ABL_PAGE_SIZE / 4u; ++i) {
    const uint8_t *d = &data[i * 4u]; // パケットのバッファは 4B 境界にないのでバイトで組み立てる
    p[i] = (uint32_t)d[0] | (uint32_t)d[1] << 8 | (uint32_t)d[2] << 16 | (uint32_t)d[3] << 24;
    FLASH->CTLR = CR_PAGE_PG | CR_BUF_LOAD;
    waitBusy();
  }
  FLASH->CTLR = CR_PAGE_PG | CR_STRT_Set;
  waitBusy();
}

// ch32fun examples/bootload と同じ手順。キーを書く前に必ずロックしておく
// （アンロック済みでキーを書き直すと、STM32 系の仕様では誤った順序とみなされるため。書き込みモードから来る場合）
void ablHalBootApp(void)
{
  FLASH->CTLR = CR_LOCK_Set;
  FLASH->KEYR = FLASH_KEY1;
  FLASH->KEYR = FLASH_KEY2;
  FLASH->BOOT_MODEKEYR = FLASH_KEY1;
  FLASH->BOOT_MODEKEYR = FLASH_KEY2;
  FLASH->STATR = 0; // MODE（bit14）= 0: 次のリセットでユーザー領域から起動
  FLASH->CTLR = CR_LOCK_Set;
  PFIC->SCTLR = 1u << 31;
  while (1) {}
}
