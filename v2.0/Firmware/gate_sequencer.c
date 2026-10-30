// Gate Sequencer v2 ファームウェア エントリポイント（CH32V003F4U6）
// V1 GateSequencer.ino の setup() / loop() に相当する
#include "ch32fun.h"
#include "hal.h"
#include "seq_app.h"

// 165（スイッチ・Push）の取り込み周期。V1 と同じ 1ms
// （Push A/B/C はデバウンスしないため、この周期がチャタリング除けを兼ねる）
#define INPUT_POLL_US 1000

int main(void)
{
  SystemInit();
  halInit(); // 595 は全消灯。以後の SPI 送受信は TIM2 割り込みが行う

  RawInputs raw = {0};
  halReadPanel(raw.in165);
  halReadFastInputs(&raw);
  appInit(&raw, halMicros(), halRandomSeed());

  AppOutputs out;
  uint32_t lastPollUs = halMicros();

  while (1) {
    halReadFastInputs(&raw);
    const uint32_t now = halMicros();
    if ((uint32_t)(now - lastPollUs) >= INPUT_POLL_US) {
      lastPollUs = now;
      halReadPanel(raw.in165);
    }

    appUpdate(&raw, now, &out);

    // 出力反映（TRIG は直結。ゲート・LED は出力が変わった周だけ渡す）
    halSetTrig(out.trigA, out.trigB);
    if (out.outputsChanged) halSetOutputs(out.outMask, out.ledMask, out.ledWeakMask, out.panelMask);
  }
}
