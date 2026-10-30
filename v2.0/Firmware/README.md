# Gate Sequencer v2 Firmware

[English](./README_EN.md)

StudioKAT の Eurorack モジュール **Gate Sequencer v2**（CH32V003F4U6）のファームウェアのソースコードです。
[ch32fun](https://github.com/cnlohr/ch32fun) を使い、C で書いています。

ソースを書き換えてビルドすれば、自分のモジュールに好きなファームウェアを書き込めます。
書き込みには、製品と同じオーディオでの更新（WAV の再生）か、WCH-LinkE を使えます。
操作方法と更新の手順は、製品のマニュアルを参照してください。

## ビルド済みのファイル

`release/` に、このソースからビルドしたファイルがあります。ソースを書き換えずに使うなら、ビルドは不要です。

| ファイル | 内容 |
|---|---|
| `release/gate_sequencer_update.wav` | オーディオでの更新に使う WAV |
| `release/gate_sequencer_factory.bin` | WCH-LinkE で書き込むイメージ（アプリ + 起動時の確認に使う情報） |
| `release/bootloader.bin` | オーディオでの更新に使うブートローダ（出荷時に書き込み済み。書き直すときだけ使う） |

書き込みの手順は、下の「書き込み」と製品のマニュアルを参照してください。

## 必要なもの

- Linux（Ubuntu、Windows の WSL2 を含む）
  - 動作を確認しているのは Ubuntu と WSL2 です
- RISC-V のクロスコンパイラと C ライブラリ
- Python 3（更新用 WAV の作成に使います。標準ライブラリだけで動きます）

```bash
sudo apt-get install -y build-essential gcc-riscv64-unknown-elf libnewlib-dev picolibc-riscv64-unknown-elf python3
```

## ch32fun を取得する

このディレクトリに、動作確認したコミットの ch32fun を置きます。

```bash
git clone https://github.com/cnlohr/ch32fun.git ch32fun
git -C ch32fun checkout a800cbd1f1ca5e250c73da83c669f0965d2052e5
```

## ビルド

```bash
make            # アプリ（gate_sequencer.bin）とブートローダ（bootloader/bootloader.bin）
make release    # 更新用 WAV と WCH-LinkE 用イメージ
```

- パスに空白を含む場所に置いた場合も、このディレクトリ内で `make` を実行してください
- `make` は最後に Flash と RAM の使用量、ブートローダの大きさを表示します
- `make release` は次の 2 つを作ります

| ファイル | 内容 |
|---|---|
| `gate_sequencer_update.wav` | オーディオでの更新に使う WAV |
| `gate_sequencer_factory.bin` | WCH-LinkE で書き込むイメージ（アプリ + 起動時の確認に使う情報） |

バージョン番号は `make release FW_VERSION=2` のように指定できます（既定は 1）。

## 書き込み

### オーディオで書き込む

`gate_sequencer_update.wav` を、製品のマニュアルの「オーディオでのアップデート」の手順で再生します。
Button A と Button B を押しながら電源を入れ、Pause IN に WAV の音を入力します。

公式のファームウェアに戻すときは、公式の更新用 WAV を同じ手順で再生してください。

### WCH-LinkE で書き込む

WCH-LinkE とモジュールのヘッダ J25 を、GND と SWIO（J25 の 1 番と 2 番）でつなぎます。
モジュールは Eurorack 電源から電源を入れておきます。

Linux で WCH-LinkE を USB から直接使える場合:

```bash
sudo apt-get install -y libusb-1.0-0-dev libudev-dev   # 初回だけ
make flash
```

- `make flash` は ch32fun 同梱の minichlink をビルドし、`gate_sequencer_factory.bin` を書き込みます
- 権限エラーになる場合は、minichlink の udev ルールを導入してください（`ch32fun/minichlink/README.md`）

Windows の場合は、ch32fun の `minichlink` フォルダにある `minichlink.exe` と `libusb-1.0.dll` を使います。
WCH-LinkE の準備（RISC-V モード、WinUSB ドライバ）は、製品のマニュアルの「WCH-LinkE でのアップデート」を参照してください。

```powershell
.\minichlink.exe -w gate_sequencer_factory.bin flash
```

> **注意**: WCH-LinkE で書き込むのは `gate_sequencer_factory.bin` です。
> `gate_sequencer.bin`（アプリだけ）を書き込むと、ブートローダがアプリを確認できず、電源を入れるたびに書き込み待ち（Step LED 1 の点滅）になります。

### ブートローダを書き込む

オーディオでの更新に使うブートローダは、出荷時に書き込み済みです。
書き直す場合だけ、次を実行します。

```bash
make flash_bootloader
```

ブートローダの仕組みと書き込みの詳細は [オーディオブートローダ](./docs/audio-bootloader.md) にあります。

## ディレクトリ構成

| パス | 内容 |
|---|---|
| `gate_sequencer.c` | エントリポイント（初期化と処理ループ） |
| `src/hal_ch32.c` / `src/hal.h` | ハード層。マイコンのペリフェラル（GPIO・SPI・TIM2・EXTI・ADC）に触れるのはこのファイルだけです |
| `src/seq_app.*` | モードの選択、エッジの処理、パネル LED の合成 |
| `src/seq_mode0.c` / `seq_mode1.c` / `seq_mode2.c` | MODE 1 / MODE 2 / MODE 3 のロジック |
| `src/seq_common.*` | 入力の解読・デバウンス、乱数、TRIG の整形 |
| `src/seq_infilter.*` | 直結入力のチャタリング対策 |
| `src/io_map.*` | 74HC595 / 74HC165 のビット配置、スイッチの判定、LED の調光 |
| `bootloader/` | オーディオブートローダと、更新用 WAV のエンコーダ（`encode_wav.py`） |
| `docs/` | ハードウェア対応表、オーディオブートローダの仕様 |

- ソース内のモード番号は 0 / 1 / 2 で、マニュアルの MODE 1 / 2 / 3 に対応します
- ピンとビット配置は [ハードウェア対応表](./docs/hardware.md) にあります

## 改造するときの注意

- アプリが使える Flash は 16320B です。最後の 64B（0x08003FC0）は、ブートローダが使うので使わないでください
  - リンカスクリプトでこの領域を除いているので、超えるとリンクエラーになります
- 設定の保存などで Flash に書き込む場合も、最後の 64B は書き換えないでください。書き換えると、次の電源投入で書き込み待ちになります
- 更新用 WAV には製品 ID が入っています。ブートローダは、Gate Sequencer v2 の ID の WAV だけを受け付けます
- ブートローダ（`bootloader/`）はブート領域（1920B）に入ります。ほぼ満杯なので、変更するとサイズを超えやすいです
- アプリの処理ループの速さは、最速 10kHz のクロックに追従するために重要です。重い処理を足すときは注意してください

## ライセンス

[MIT License](./LICENSE) です。

ビルドに使う [ch32fun](https://github.com/cnlohr/ch32fun) も MIT License です（このリポジトリには含みません）。
