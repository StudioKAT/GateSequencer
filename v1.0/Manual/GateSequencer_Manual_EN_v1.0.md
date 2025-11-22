# Gate Sequencer Manual (English)

![Gate Sequencer Front](../../Images/GateSequencer_v1.0_Front.jpg)

## Overview

The **Gate Sequencer** is a multi-functional gate sequencer equipped with toggle switches. It features three distinct functions that can be switched via a single mode switch:
* **8-Step Sequencer x2** running on independent clocks.
* **16-Step Sequencer** with performance features.
* **Shift Register (Turing Machine)** with variable length and routing capabilities.

---

## Specifications

- **Format**: Eurorack
- **Width**: 16 HP
- **Depth**: 25 mm
- **Current Draw**: +12V: 50mA / -12V: 0mA

> **Note**: Please ensure the red stripe on the ribbon cable is connected to -12V.
> Incorrect connection may damage the module.
> **Always check cable orientation before powering on.**

---

## Functions of Parts

![Gate Sequencer Ex](../../Images/GateSequencer_v1.0_Ex.jpg)

1.  **Probability Switch (1-16)**  
    3-position toggle switches that set the behavior for each step.

    **[In MODE 1 & 2 (Sequencer Modes)]**
    Sets the probability of a gate output.
    - **L**: **OFF** (0%) - No gate output.
    - **C**: **PROB** (50%) - 50% chance of gate output.
    - **R**: **ON** (100%) - Always outputs a gate.

    **[In MODE 3 (Shift Register Mode)]**
    Sets the circuit connection and gate output.
    - **L**: **SKIP** - Removes the step from the circuit (shortens the sequence length).
    - **C**: **MUTE** - Data passes through, but no Gate OUT is generated.
    - **R**: **ACTIVE** - Data passes through and generates a Gate OUT.

2.  **Step LED (1-16)**  
    Displays the current step position or gate status.  
    In MODE 3, it indicates the presence of data.  

3.  **Direction Switch**  
    Sets the playback direction of the sequence.
    - **L**: **Forward** - Advances from 1 → 16.
    - **C**: **PingPong** - Plays back and forth.
    - **R**: **Random** - Moves to steps randomly.

4.  **Mode Switch**
    Switches the operation mode of the module.
    - **L**: **MODE 1** (Dual 8 Step Sequencer)
    - **C**: **MODE 2** (16 Step Sequencer)
    - **R**: **MODE 3** (Shift Register)

5.  **Clock Button / Clock IN**
    Clock input to advance the sequence.
    - **Button**: Manually advances the clock.
    - **IN**: Input for external clock.

6.  **Button A, B, C / IN A, B, C**  
    Multi-function inputs where the function changes based on the selected mode.  
    - **A**: Fill / Data Input / Clock B, etc.
    - **B**: Reset, etc.
    - **C**: Mute / XOR Input / Reset B, etc.  
    (See the "Operation Modes" section for details).

7.  **Pause IN**  
    When the input is HIGH (+5V), the clock is ignored, and the sequence pauses.

8.  **Step Gate OUT (1-16)**  
    Outputs gate signals (+5V) for each step.

9.  **Trig A, B OUT**  
    Outputs auxiliary triggers or logic signals depending on the mode.
    (See the "Operation Modes" section for details).

---

## Operation Modes

### MODE 1: 8×2 Step Sequencer (Dual Mode)
Channels 1-8 and 9-16 operate as two independent 8-step sequencers. This is ideal for creating polyrhythms or two-voice patterns.

| Input/Output | Function Description |
| --- | --- |
| **SW 1-8** | Probability settings for Sequencer A (ch.1-8). |
| **SW 9-16** | Probability settings for Sequencer B (ch.9-16). |
| **IN A / Button A** | **Clock B**: Clock input for Sequencer B.<br>*Note:* Pressing **Button A** toggles "Sync Mode" ON/OFF.<br>When ON (LED A lit): Sequencer B advances using the same clock as Sequencer A. |
| **IN B / Button B** | **Reset A**: Resets Sequencer A to step 1. |
| **IN C / Button C** | **Reset B**: Resets Sequencer B to step 1.<br>*Note:* Pressing **Button C** toggles "Reset Sync" ON/OFF.<br>When ON (LED C lit): Input to IN B resets both sequencers simultaneously. |
| **Trig A OUT** | Outputs the gate signal for Sequencer A. |
| **Trig B OUT** | Outputs the gate signal for Sequencer B. |

---

### MODE 2: 16 Step Sequencer (Single Mode)
Operates as a single 16-step sequencer. This mode is specialized for real-time performance using button operations.

| Input/Output | Function Description |
| --- | --- |
| **SW 1-16** | Probability settings for each step. |
| **IN A / Button A** | **FILL**: While held (or input is high), forces all step gates to ON. |
| **IN B / Button B** | **RESET**: Resets to step 1. |
| **IN C / Button C** | **MUTE**: While held (or input is high), forces all step gates to OFF. |
| **Trig A OUT** | **Inverted Gate**: Outputs a trigger only on steps where the Gate is OFF. |
| **Trig B OUT** | **Normal Gate**: Outputs a trigger on steps where the Gate is ON. |

---

### MODE 3: Shift Register (Turing Machine Style)
This mode circulates input data within an internal 16-bit register. It operates as a random generator similar to a "Turing Machine," but features **variable bit length and routing changes based on step switch settings.**

| Input/Output | Function Description |
| --- | --- |
| **SW 1-16** | **Circuit Topology Settings**<br>**L (Skip)**: Ignores the bit and connects the preceding/succeeding steps directly (shortens loop length).<br>**C (Mute)**: Data passes through, but no Gate output is generated.<br>**R (Active)**: Data passes through and generates a Gate output. |
| **IN A / Button A** | **DATA IN**: Source for new data. Pressing Button A manually injects a "1". |
| **IN B / Button B** | **CLEAR**: Resets the register content to all 0s. |
| **IN C / Button C** | **XOR IN / DELETE**: <br>Normally performs an **XOR (Exclusive OR)** with "IN A" to determine the next data bit (creating evolving patterns).<br>Holding **Button C** forces written data to "0" (Data Delete). |
| **Trig A OUT** | **NOT DATA**: Outputs the inverted signal of the written data. |
| **Trig B OUT** | **DATA**: Outputs the written data (XOR result). |
| **Direction SW** | **PingPong**: Data (token) moves back and forth between active steps.<br>**Random**: Randomly shuffles the connection order between active steps. |

---

## License

This work is licensed under [Creative Commons Attribution-ShareAlike 4.0 International (CC BY-SA 4.0)](https://creativecommons.org/licenses/by-sa/4.0/).

---

## Credits

- **Designed by**: StudioKAT
- **Website**: [https://www.studiokat.jp/](https://www.studiokat.jp/)
- **GitHub**: [https://github.com/StudioKAT](https://github.com/StudioKAT)
- **X (Twitter)**: [https://x.com/StudioKAT_synth](https://x.com/StudioKAT_synth)
- **Instagram**: [https://www.instagram.com/studiokat_modular/](https://www.instagram.com/studiokat_modular/)

---

## Update History

- `2025-11-22`: v1.0 Release