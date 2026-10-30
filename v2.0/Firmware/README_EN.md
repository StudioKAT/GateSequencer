# Gate Sequencer v2 Firmware

[日本語](./README.md)

Firmware source code for **Gate Sequencer v2** (CH32V003F4U6), a Eurorack module by StudioKAT.
It is written in C with [ch32fun](https://github.com/cnlohr/ch32fun).

You can modify the source, build it, and write your own firmware to your module.
You can write it with the same audio update as the official firmware (playing a WAV file), or with a WCH-LinkE.
See the product manual for operation and update procedures.

## Prebuilt Files

`release/` contains files built from this source. If you do not modify the source, you do not need to build anything.

| File | Contents |
|---|---|
| `release/gate_sequencer_update.wav` | WAV for the audio update |
| `release/gate_sequencer_factory.bin` | Image to write with the WCH-LinkE (application + information used for the check at startup) |
| `release/bootloader.bin` | Bootloader for the audio update (written at the factory; use it only to rewrite the bootloader) |

For the writing procedure, see "Writing the Firmware" below and the product manual.

## Requirements

- Linux (Ubuntu, including WSL2 on Windows)
  - Tested on Ubuntu and WSL2
- RISC-V cross compiler and C library
- Python 3 (used to create the update WAV; only the standard library is required)

```bash
sudo apt-get install -y build-essential gcc-riscv64-unknown-elf libnewlib-dev picolibc-riscv64-unknown-elf python3
```

## Getting ch32fun

Place ch32fun at the tested commit in this directory.

```bash
git clone https://github.com/cnlohr/ch32fun.git ch32fun
git -C ch32fun checkout a800cbd1f1ca5e250c73da83c669f0965d2052e5
```

## Building

```bash
make            # Application (gate_sequencer.bin) and bootloader (bootloader/bootloader.bin)
make release    # Update WAV and WCH-LinkE image
```

- Run `make` inside this directory, even if the path contains spaces
- At the end, `make` shows the Flash and RAM usage and the bootloader size
- `make release` creates the following two files

| File | Contents |
|---|---|
| `gate_sequencer_update.wav` | WAV for the audio update |
| `gate_sequencer_factory.bin` | Image to write with the WCH-LinkE (application + information used for the check at startup) |

You can set the version number with `make release FW_VERSION=2` (default: 1).

## Writing the Firmware

### Writing via Audio

Play `gate_sequencer_update.wav` following "Updating via Audio" in the product manual.
Hold Button A and Button B while powering on, and feed the WAV into Pause IN.

To return to the official firmware, play the official update WAV in the same way.

### Writing via WCH-LinkE

Connect the WCH-LinkE to the module's header J25 with GND and SWIO (pins 1 and 2 of J25).
Power the module from the Eurorack power supply.

On Linux, if the WCH-LinkE is directly accessible over USB:

```bash
sudo apt-get install -y libusb-1.0-0-dev libudev-dev   # first time only
make flash
```

- `make flash` builds minichlink bundled with ch32fun and writes `gate_sequencer_factory.bin`
- If you get a permission error, install the minichlink udev rules (`ch32fun/minichlink/README.md`)

On Windows, use `minichlink.exe` and `libusb-1.0.dll` from the `minichlink` folder of ch32fun.
For preparing the WCH-LinkE (RISC-V mode, WinUSB driver), see "Updating via WCH-LinkE" in the product manual.

```powershell
.\minichlink.exe -w gate_sequencer_factory.bin flash
```

> **Note**: Write `gate_sequencer_factory.bin` with the WCH-LinkE.
> If you write `gate_sequencer.bin` (the application only), the bootloader cannot verify the application, and the module waits for data (Step LED 1 blinking) every time it is powered on.

### Writing the Bootloader

The bootloader used for the audio update is written at the factory.
Run the following only if you need to rewrite it.

```bash
make flash_bootloader
```

The bootloader design and writing details are in [Audio Bootloader](./docs/audio-bootloader.md) (Japanese).

## Directory Layout

| Path | Contents |
|---|---|
| `gate_sequencer.c` | Entry point (initialization and main loop) |
| `src/hal_ch32.c` / `src/hal.h` | Hardware layer. Only this file touches the MCU peripherals (GPIO, SPI, TIM2, EXTI, ADC) |
| `src/seq_app.*` | Mode selection, edge handling, panel LED composition |
| `src/seq_mode0.c` / `seq_mode1.c` / `seq_mode2.c` | Logic for MODE 1 / MODE 2 / MODE 3 |
| `src/seq_common.*` | Input decoding and debouncing, random numbers, TRIG shaping |
| `src/seq_infilter.*` | Chatter filtering for the direct inputs |
| `src/io_map.*` | 74HC595 / 74HC165 bit layout, switch decoding, LED dimming |
| `bootloader/` | Audio bootloader and the update WAV encoder (`encode_wav.py`) |
| `docs/` | Hardware map and audio bootloader specification (Japanese) |

- Mode numbers in the source are 0 / 1 / 2, corresponding to MODE 1 / 2 / 3 in the manual
- The pin and bit layout is in the [Hardware Map](./docs/hardware.md) (Japanese)
- Comments in the source code are in Japanese

## Notes on Modifying

- The application can use 16320 bytes of Flash. Do not use the last 64 bytes (0x08003FC0); the bootloader uses them
  - The linker script excludes this area, so exceeding the limit causes a link error
- If you write to Flash (for example, to save settings), do not modify the last 64 bytes. Otherwise, the module waits for data at the next power-on
- The update WAV contains a product ID. The bootloader accepts only WAV files with the Gate Sequencer v2 ID
- The bootloader (`bootloader/`) fits in the boot area (1920 bytes). It is almost full, so changes can easily exceed the size
- The speed of the main loop matters for following clocks up to 10kHz. Be careful when adding heavy processing

## License

This firmware is licensed under the [MIT License](./LICENSE).

[ch32fun](https://github.com/cnlohr/ch32fun), used for building, is also licensed under the MIT License (it is not included in this repository).
