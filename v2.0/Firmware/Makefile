# Gate Sequencer v2 ファームウェア（CH32V003F4U6 / ch32fun）
#
#   make          CH32V003 向けにクロスビルド（アプリ gate_sequencer.bin とブートローダ bootloader/bootloader.bin）
#   make release  WCH-LinkE 用イメージ gate_sequencer_factory.bin と更新用 WAV gate_sequencer_update.wav を生成
#   make flash    WCH-LinkE でアプリを書き込み（minichlink。gate_sequencer_factory.bin を書く）
#   make flash_bootloader  WCH-LinkE でブートローダとオプションバイトを書き込み
#
# パスに空白を含むため、必ずこのディレクトリ内で make を実行すること（相対パス前提）。

all : build bootloader

TARGET := gate_sequencer
TARGET_MCU ?= CH32V003
# ファームのバージョン（更新用 WAV とメタデータに入る。リリースのたびに上げる）
FW_VERSION ?= 1

# アプリは Flash の最後の 64B ページを使わない（オーディオブートローダのメタデータ置き場）。
# ch32fun が生成するリンカスクリプトの Flash 長を 16K から 16320 に書き換えて使う
LINKER_SCRIPT := build/app_ch32v003.ld

# 動作確認した ch32fun のコミット
CH32FUN_COMMIT := a800cbd1f1ca5e250c73da83c669f0965d2052e5

# ハード非依存ロジック（PC テストと共用）とハード層
LOGIC_SRCS := $(filter-out src/hal_ch32.c,$(wildcard src/*.c))
ADDITIONAL_C_FILES := $(wildcard src/*.c)
EXTRA_CFLAGS += -Isrc


ifeq ($(wildcard ch32fun/ch32fun/ch32fun.mk),)
$(error ch32fun がありません。このディレクトリで次を実行してください: git clone https://github.com/cnlohr/ch32fun.git ch32fun && git -C ch32fun checkout $(CH32FUN_COMMIT))
endif
include ch32fun/ch32fun/ch32fun.mk

$(LINKER_SCRIPT) : $(GENERATED_LD_FILE)
	@mkdir -p build
	sed 's/^\(\s*FLASH (rx) : ORIGIN = 0x00000000, LENGTH = \)16K$$/\116320/' $< > $@
	@grep -q 'LENGTH = 16320' $@ || { echo "リンカスクリプトの Flash 長を書き換えられない" >&2; rm -f $@; exit 1; }

# オーディオブートローダ（ブート領域 1920B。超えるとリンクエラー）
bootloader :
	$(MAKE) -C bootloader
	@echo "bootloader.bin: $$(wc -c < bootloader/bootloader.bin) / 1920 B（ブート領域）"

# 工場用イメージ（アプリ + メタデータ。ブートローダとは別に書く）と更新用 WAV
release : build
	python3 bootloader/encode_wav.py $(TARGET).bin $(TARGET)_update.wav --version $(FW_VERSION) --factory $(TARGET)_factory.bin

# アプリだけの .bin を書くと、ブートローダがメタデータと合わないと判定して書き込み待ちになる。
# そのため、メタデータを含む工場用イメージを書く
flash : release $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -w $(TARGET)_factory.bin flash -b

# ブートローダ（ブート領域）と、電源投入時にブート領域から起動するオプションバイト（USER = 0x37）
flash_bootloader : bootloader $(MINICHLINK)/minichlink
	$(MINICHLINK)/minichlink -a -U -w bootloader/bootloader.bin bootloader -b
	$(MINICHLINK)/minichlink -w +a55a37c8 option

clean : cv_clean
	-$(RM) -r build
	$(MAKE) -C bootloader clean

.PHONY : bootloader release flash flash_bootloader

