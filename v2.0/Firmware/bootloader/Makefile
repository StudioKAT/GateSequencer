# オーディオブートローダ（ブート領域 0x1FFFF000、1920B）
#
#   make        bootloader.bin を生成（サイズが 1920B を超えるとリンクエラー）
#
# 書き込みとオプションバイトの設定は docs/audio-bootloader.md。
# パスに空白を含むため、必ずこのディレクトリ内で make を実行すること（相対パス前提）。

all : build

TARGET := bootloader
TARGET_MCU ?= CH32V003
LINKER_SCRIPT := ../ch32fun/ch32fun/ch32v003fun-bootloader.ld
WRITE_SECTION := bootloader
FLASH_COMMAND = $(MINICHLINK)/minichlink -a -U -w $(TARGET).bin $(WRITE_SECTION) -b

ADDITIONAL_C_FILES := abl_demod.c abl_packet.c abl_hal.c ../src/io_map.c
EXTRA_CFLAGS += -I../src

include ../ch32fun/ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean
