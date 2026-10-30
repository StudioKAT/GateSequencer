# Gate Sequencer v2.0 マニュアル（日本語）

![Gate Sequencer Front](../Images/GateSequencer_v2.0_Front.jpg)

## 概要

**Gate Sequencer** は、トグルスイッチ式の多機能ゲートシーケンサーです。  
・独立した2つのクロックで動く **8 Stepシーケンサー x2**  
・パフォーマンス機能を備えた **16 Stepシーケンサー**  
・可変長・経路変更可能な **シフトレジスタ (Turing Machine)**  
の3つの機能を備え、スイッチ一つで切り替えて使用できます。


---

## 仕様

- **フォーマット**：Eurorack
- **幅**：14 HP
- **奥行き**：25 mm
- **消費電流**：+12V: 50mA / -12V: 0mA
- **クロック入力**：最大 10kHz

> ※ ケーブルの赤いラインが -12V に接続されていることを確認してください。
> 誤接続するとモジュールが故障する可能性があります。
> **電源投入前にケーブルの向きを必ずご確認ください。**

---

## 各部の機能

![Gate Sequencer Ex](../Images/GateSequencer_v2.0_Ex.jpg)

1. **Step Switch (1-16)**
    各ステップの挙動を設定する3ポジショントグルスイッチです。
    L / C / R の意味はモードごとに異なります。

    | 位置 | MODE 1 (8×2) | MODE 2 (16) | MODE 3 (Shift Register) |
    | :--- | :--- | :--- | :--- |
    | **L** | **PROB** (50%) | **TRIG A** | **MUTE** |
    | **C** | **OFF** | **OFF** | **SKIP** |
    | **R** | **ON** (100%) | **TRIG B** | **ACTIVE** |

    （詳細は「動作モード」セクションを参照）

2. **Step LED (1-16)**
    現在のステップ位置やゲートの状態を表示します。
    - MODE 1・2 では、到達したステップが点灯します。
      - そのステップで TRIG が出る場合は強点灯、出ない場合は弱点灯します。
    - MODE 3 では、データ（1）があるビットが点灯します。
      - ACTIVE のステップは強点灯、MUTE のステップは弱点灯します。

3. **Direction Switch**
    シーケンスの進行方向を設定します。
    - **L** : **Forward** - 1 → 16 の順に進みます。
    - **C** : **PingPong** - 往復動作を行います。両端のステップは2回続けて鳴ります。
    - **R** : **Random** - 現在のステップ以外の全ステップから、ランダムに次のステップを選びます。

4. **Mode Switch**
    モジュールの動作モードを切り替えます。
    - **L** : **MODE 1** (Dual 8 Step Sequencer)
    - **C** : **MODE 2** (16 Step Sequencer)
    - **R** : **MODE 3** (Shift Register)

    モードを切り替えると、シーケンスはステップ1から始まります（MODE 3 ではレジスタが空になります）。

5. **Clock Button / Clock IN**
    シーケンスを進めるためのクロック入力です。
    - **Button** : 手動でクロックを進めます。
    - **IN** : 外部クロックを入力します。

6. **Button A, B, C / IN A, B, C**
    選択中のモードによって機能が変化する多機能入力です。
    - **A** : Clock B / Fill / Data Input
    - **B** : Reset / Clear
    - **C** : Reset B / Mute / XOR Input
    （詳細は「動作モード」セクションを参照）

    電源投入時に **Button A と Button B を同時に押している**と、ファームウェアの更新モードに入ります（「ファームウェアのアップデート」を参照）。

7. **Pause IN**
    入力がHIGH(+5V)の間、シーケンスの進行を一時停止します。
    - クロックとリセットを受け付けず、TRIG 出力を止めます。
    - Step Gate OUT は、一時停止したときの状態を保ちます。

    ファームウェアの更新時は、このジャックに更新用の音声を入力します。

8. **Step Gate OUT (1-16)**
    各ステップのゲート信号(+5V)を出力します。

9. **Trig A, B OUT**
    モードに応じたトリガーや論理信号を出力します。
    - 出力の幅はクロックの HIGH の長さに従います。短いクロックでも、最低 3ms の幅で出力します。
    （詳細は「動作モード」セクションを参照）

10. **Panel LED**
    入出力の状態を表示します。

    | LED | 表示 |
    | :--- | :--- |
    | **CLK** | Clock IN が HIGH、または Clock Button を押している間点灯 |
    | **A / B / C** | IN A/B/C が HIGH、または Button A/B/C を押している間点灯（MODE 1 では Sync の状態も表示） |
    | **PAUSE** | Pause IN が HIGH の間点灯 |
    | **OUT A / OUT B** | Trig A / Trig B OUT が HIGH の間点灯 |


---

## 動作モード

### MODE 1 : 8×2 Step Sequencer (Dual Mode)
1-8番と9-16番が独立した2系統の8ステップシーケンサーとして動作します。ポリリズムや2声のパターンの作成に適しています。

| 入出力 | 機能説明 |
| :--- | :--- |
| **SW 1-8** | シーケンサーA (ch.1-8) の設定<br>**L (PROB)**: 50%の確率で TRIG A を出力します。<br>**C (OFF)**: TRIG A を出力しません。<br>**R (ON)**: 常に TRIG A を出力します。 |
| **SW 9-16** | シーケンサーB (ch.9-16) の設定。L / C / R の意味は SW 1-8 と同じで、TRIG B に対応します。 |
| **Step Gate OUT** | 到達したステップで、スイッチの位置や抽選結果に関係なく HIGH になります。 |
| **IN A** | **Clock B** : シーケンサーBのクロック入力。 |
| **Button A** | **Sync** のON/OFFを切り替えます。<br>ON時(LED A点灯): シーケンサーBは、IN A ではなく Clock (シーケンサーAと同じクロック) で進みます。 |
| **IN B / Button B** | **Reset A** : シーケンサーAをステップ1に戻します。Button B を押している間は、ステップ1に留まります。 |
| **IN C** | **Reset B** : シーケンサーBをステップ1に戻します。 |
| **Button C** | **Reset Sync** のON/OFFを切り替えます。<br>ON時(LED C点灯): Reset A (IN B / Button B) で両方のシーケンサーがリセットされます。 |
| **Trig A OUT** | シーケンサーAの抽選に当たったステップで、Clock に合わせて TRIG を出力します。 |
| **Trig B OUT** | シーケンサーBの抽選に当たったステップで、Clock B (Sync ON 時は Clock) に合わせて TRIG を出力します。 |

- Sync と Reset Sync の設定は保存されません。電源投入時は OFF で、モードを切り替えても保たれます。
- Clock Button で進めたときは、抽選に当たったステップで短い TRIG を出力します。

---

### MODE 2 : 16 Step Sequencer (Single Mode)
16ステップのシングルシーケンサーとして動作します。各ステップの TRIG を A / B に振り分けられ、ボタン操作によるリアルタイムパフォーマンスにも対応します。

| 入出力 | 機能説明 |
| :--- | :--- |
| **SW 1-16** | 各ステップの TRIG の出力先<br>**L (TRIG A)**: Trig A OUT から出力します。<br>**C (OFF)**: TRIG を出力しません。<br>**R (TRIG B)**: Trig B OUT から出力します。 |
| **Step Gate OUT** | 到達したステップで HIGH になります（MUTE 中を除く）。 |
| **IN A / Button A** | **FILL** : 入力がある間（押している間）、全ステップを R (TRIG B) として扱います。 |
| **IN B / Button B** | **RESET** : ステップ1にリセットします。Button B を押している間は、ステップ1に留まります。 |
| **IN C / Button C** | **MUTE** : 入力がある間（押している間）、全ステップを C (OFF) として扱い、Step Gate OUT も LOW にします。FILL より優先します。 |
| **Trig A OUT** | L のステップで、Clock に合わせて TRIG を出力します。 |
| **Trig B OUT** | R のステップ（FILL 中は全ステップ）で、Clock に合わせて TRIG を出力します。 |

---

### MODE 3 : Shift Register (Turing Machine Style)
入力されたデータを内部の16bitレジスタ内で循環させるモードです。いわゆる「Turing Machine」のようなランダム生成器として動作しますが、**各ステップのスイッチ設定によってビット長や経路が変化する**のが特徴です。

| 入出力 | 機能説明 |
| :--- | :--- |
| **SW 1-16** | **回路トポロジー設定**<br>**L (Mute)**: データは通過しますが、Gate出力はしません（LED は弱点灯）。<br>**C (Skip)**: そのビットを無視し、前後のステップを直結します（ループ長短縮）。<br>**R (Active)**: データ通過＋Gate出力（LED は強点灯）。 |
| **Step Gate OUT** | ACTIVE のステップで、データが 1 の間 HIGH になります。 |
| **IN A / Button A** | **DATA IN** : 新しいデータの入力源です。Button Aを押している間は「1」を注入します。 |
| **IN B / Button B** | **CLEAR** : レジスタの内容を全て0にリセットします。 |
| **IN C** | **XOR IN** : 「DATA IN」との **XOR (排他的論理和)** を取って、書き込むデータを決定します。 |
| **Button C** | **DELETE** : 押している間は、書き込むデータを強制的に「0」にします（パターンの消去）。 |
| **Trig B OUT** | **DATA OUT** : 経路の最後のステップ（レジスタの出口）のデータを出力します。 |
| **Trig A OUT** | **NOT DATA OUT** : Trig B OUT の反転信号を出力します。 |
| **Direction SW** | **Forward**: データは有効なステップ（MUTE / ACTIVE）を 1 → 16 の順に進みます。<br>**PingPong**: データ（トークン）が有効なステップ間を往復し、先頭に戻ると消えます。<br>**Random**: 有効なステップ間の接続順序をランダムに並べ替えます（最初と最後のステップは固定）。スイッチの設定を変えるか CLEAR すると、並べ替え直します。 |

> **ループの作り方**: Trig B OUT を IN C にパッチすると、レジスタの出口のデータが入口に戻り、有効なステップの数を周期にパターンがループします。IN A / Button A でデータを足し、Button C で消せます。

---

## ファームウェアのアップデート

ファームウェアは、次のどちらかの方法で更新できます。

| 方法 | 必要なもの | 用途 |
| :--- | :--- | :--- |
| **オーディオ** | 更新用 WAV ファイル、WAV を再生できる機器、3.5mm オーディオケーブル | 通常の更新 |
| **WCH-LinkE** | WCH-LinkE（書き込み器）、Windows PC、ジャンパワイヤ | オーディオで更新できないときの復旧 |

### オーディオでのアップデート

パソコンやオーディオインターフェースで WAV ファイルを再生し、その音を **Pause IN** に入力して更新します。専用の書き込み器は不要です。

#### 用意するもの

- 更新用の WAV ファイル
- WAV を再生できる機器。次のどちらかを使ってください。
  - オーディオインターフェースのライン出力（おすすめ）
  - パソコンのヘッドホン出力
- 3.5mm のオーディオケーブル（出力機器の端子 → Pause IN）

#### 更新の前に

- 再生する機器の音量を **最大** にしてください。
- イコライザ、音量の自動調整、空間オーディオなどの **音質補正はすべて切って** ください。
- 音が小さいと更新できません。パソコンのヘッドホン出力では音量がぎりぎりのことがあります。
  - うまくいかないときは、オーディオインターフェースのライン出力を使ってください。
  - または、ゲインのあるモジュール（ミキサー、VCA など）を通して、音を大きくしてから Pause IN に入れてください。
- Bluetooth などのワイヤレス出力は使わず、ケーブルで接続してください。
- 更新中は、ほかの音（通知音など）が鳴らないようにしてください。

#### 手順

1. ケースの電源を切ります。
2. 出力機器と、モジュールの **Pause IN** をケーブルでつなぎます。
3. **Button A と Button B を両方押したまま**、ケースの電源を入れます。
4. Step LED 1 が点滅したら、ボタンから手を離します（書き込み待ちの状態です）。
5. 更新用の WAV ファイルを最初から最後まで再生します（約 40 秒）。
    - 音を受けている間は、PAUSE LED が点灯します。
    - Step LED が、左から順に進捗を表示します。
6. すべての Step LED が約 2 秒点灯した後、新しいファームウェアで動き出したら完了です。

更新中は、シーケンサーとしては動作しません。

#### うまくいかないとき

| 症状 | 対処 |
| :--- | :--- |
| 電源を入れても Step LED 1 が点滅しない | Button A と Button B を両方押したまま電源を入れてください。電源を入れた後にボタンを押しても、更新モードには入りません。 |
| 再生しても PAUSE LED が点かない | 音が届いていません。ケーブルの接続先（Pause IN）と、音量が最大になっているかを確かめてください。 |
| PAUSE LED が点いたり消えたりして、進捗が進まない | 音が小さいか、音質補正がかかっています。音量を最大にし、音質補正を切ってください。ライン出力やゲインのあるモジュールを使ってください。 |
| 全 LED が点滅する | 更新できませんでした（音の途切れ、別製品の WAV など）。電源は入れたまま、WAV を最初から再生し直してください。 |
| 途中で再生が止まった・電源が切れた | 電源を入れ直すと、ボタンを押さなくても書き込み待ちの状態になります。WAV を最初から再生し直してください。 |

更新が途中で止まっても、モジュールが使えなくなることはありません。
もう一度最後まで再生すれば、更新が完了します。

### WCH-LinkE でのアップデート

WCH-LinkE（WCH 社の書き込み器）でモジュールのマイコンに直接書き込みます。
オーディオでの更新がどうしてもうまくいかないときに使ってください。
ここでは、Windows でオープンソースの書き込みツール **minichlink** を使う手順を説明します。

#### 用意するもの

- WCH-LinkE
- Windows PC
- ジャンパワイヤ 2 本（WCH-LinkE → モジュールの J25）
- WCH-LinkE 用のファームウェアファイル（`gate_sequencer_factory.bin`）
- 書き込みツール `minichlink.exe` と `libusb-1.0.dll`
  - [ch32fun](https://github.com/cnlohr/ch32fun) リポジトリの `minichlink` フォルダにあります。2 つを同じフォルダ（例: `C:\wch-linke-flash`）に置いてください。
  - 実行には Microsoft Visual C++ 再頒布可能パッケージ（x64）が必要です。

> **注意**: WCH-LinkE で書き込むファイルは、WAV ではなく `gate_sequencer_factory.bin` です。
> このファイルには、起動時の確認に使う情報が含まれています。ほかの `.bin` を書き込むと、モジュールは電源を入れるたびに書き込み待ち（Step LED 1 の点滅）になります。その場合は `gate_sequencer_factory.bin` を書き込み直してください。

#### 初回だけ: WCH-LinkE の準備

1. **RISC-V モードにする**
    - WCH-LinkE を USB につなぎ、青い LED が点灯したままなら ARM モードです。
    - USB を抜き、**ModeS ボタンを押したまま** USB を挿すと RISC-V モードに切り替わります（青い LED が消えます）。切り替えたモードは記憶されます。
2. **ドライバを入れる**
    - [Zadig](https://zadig.akeo.ie/) を起動し、Options → List All Devices にチェックを入れます。
    - 一覧から WCH-Link の **Interface 0** を選び、ドライバを **WinUSB** にして Install Driver（または Replace Driver）を押します。
3. **動作を確認する**
    - WCH-LinkE だけを USB につなぎ、PowerShell で次を実行します。

    ```powershell
    cd C:\wch-linke-flash
    .\minichlink.exe
    ```

    - `Found WCH Link` と表示されれば準備は完了です（モジュールをつないでいないので、その後のエラーは問題ありません）。

#### 接続

モジュールのヘッダ **J25** と WCH-LinkE をつなぎます。

| J25 | 信号 | WCH-LinkE |
| :--- | :--- | :--- |
| 1 | GND | GND |
| 2 | SWIO | SWDIO |
| 3 | NRST | 接続不要 |

- J25 には電源ピンがありません。**モジュールを Eurorack 電源につなぎ、電源を入れた状態で**書き込みます。
- WCH-LinkE の 3.3V / 5V 出力ピンはモジュールにつながないでください。

#### 書き込み

```powershell
cd C:\wch-linke-flash
.\minichlink.exe -w gate_sequencer_factory.bin flash
```

書き込みが終わったら、ケースの電源を入れ直してください。新しいファームウェアで動き出せば完了です。

#### 更新モードの書き直し（上級者向け）

オーディオでの更新モードそのもの（ブートローダ）が動かなくなった場合は、WCH-LinkE で書き直せます。
ファームウェアファイルと一緒に配布される `bootloader.bin` を使い、次の 3 行を順に実行します。

```powershell
.\minichlink.exe -w gate_sequencer_factory.bin flash
.\minichlink.exe -a -U -w bootloader.bin bootloader -b
.\minichlink.exe -w +a55a37c8 option
```

- 3 行目は、電源投入時に更新モードのプログラムから起動するための設定（オプションバイト）です。値を変えずに入力してください。
- `.\minichlink.exe -r + option 16` で読み戻し、先頭が `a5 5a 37 c8` になっていれば正しく設定されています。
- 書き込みが終わったら、ケースの電源を入れ直してください。

---

## ライセンス

この作品は [Creative Commons 表示 - 継承 4.0 国際 (CC BY-SA 4.0)](https://creativecommons.org/licenses/by-sa/4.0/deed.ja) の下でライセンスされています。

---

## クレジット

- **Designed by**：StudioKAT
- **Website**：[https://www.studiokat.jp/](https://www.studiokat.jp/)
- **GitHub**：[https://github.com/StudioKAT](https://github.com/StudioKAT)
- **X（旧Twitter）**：[https://x.com/StudioKAT_synth](https://x.com/StudioKAT_synth)
- **Instagram**：[https://www.instagram.com/studiokat_modular/](https://www.instagram.com/studiokat_modular/)

---

## 更新履歴

- `2026-10-29`：v2.0 リリース
  - 全体構成見直しにより幅が16HP→14HPにコンパクト化  
  - マイコンを CH32V003 に変更  
  - ステップスイッチの L / C / R の割り当てをモードごとに変更  
  - MODE 2 で TRIG A / TRIG B をステップごとに振り分けられるように変更  
  - MODE 3 の Trig A / B OUT を、レジスタの出口のデータに変更  
  - PAUSE / OUT A / OUT B のパネル LED を追加  
  - オーディオによるファームウェアのアップデートに対応  
  - コンフィグモードを廃止（Step Gate OUT は v1.x の Mode A に固定）  
