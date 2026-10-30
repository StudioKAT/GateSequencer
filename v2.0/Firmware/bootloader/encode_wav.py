#!/usr/bin/env python3
"""アプリの .bin から、オーディオブートローダ用の更新 WAV を作る（標準ライブラリだけで動く）。

    python3 encode_wav.py gate_sequencer.bin gate_sequencer_update.wav --version 2 [--factory factory.bin]

--factory を付けると、工場で WCH-LinkE で書く 16KB のイメージ（アプリ + 0xFF 埋め + 最後のページに
メタデータ）も作る。メタデータは、ブートローダが更新の最後に書くもの（END パケットの先頭 64B）と同じ。

形式（ブートローダの bootloader/abl_demod.h・abl_packet.h と対応）:
  - 正弦波 2 値 FSK、1 ビット = 1 周期。0 = 6kHz、1 = 3kHz。バイトは LSB から
  - パケット = プリアンブル（0 の連続）+ 同期語 0x2DD4 + 種別 + 本体（65B 固定、START / END は 0 埋め）
    + CRC32（zlib.crc32）
    + ポストアンブル（0 を数ビット）。1 ビットの長さは次の周期の立ち上がりで決まるため、
    ポストアンブルがないとパケットの最後のビットが確定しない
  - 並び: [START, DATA x ページ数] を passes 回 → END
  - 各パケットの後に無音（gap_ms）を入れる。ブートローダが Flash を書く間は音を拾えないため
"""
import argparse
import math
import struct
import sys
import wave
import zlib

SYNC = 0x2DD4
PRODUCT_ID = 0x4753
PAGE_SIZE = 64
APP_MAX = 16384 - PAGE_SIZE
PKT_START, PKT_DATA, PKT_END = 1, 2, 3
BODY_SIZE = 1 + PAGE_SIZE    # 本体の長さ（全種別で固定）
F0, F1 = 6000, 3000          # ビット 0 / 1 の周波数
PREAMBLE_BITS = 48
POSTAMBLE_BITS = 4
LEAD_S = 0.5                 # 先頭・末尾の無音


def packet_bits(body):
    """種別 + 本体に CRC32 を付け、プリアンブルと同期語を前に付けたビット列"""
    data = bytes(body) + struct.pack("<I", zlib.crc32(bytes(body)) & 0xFFFFFFFF)
    bits = [0] * PREAMBLE_BITS
    for byte in struct.pack("<H", SYNC) + data:
        bits += [(byte >> i) & 1 for i in range(8)]
    return bits + [0] * POSTAMBLE_BITS


def info_body(ptype, product, version, app):
    info = struct.pack("<HHHI", product, version, len(app), zlib.crc32(app) & 0xFFFFFFFF)
    return bytes([ptype]) + info + bytes(BODY_SIZE - len(info))


class Synth:
    """1 周期ずつ正弦波を並べる（周期の境目で位相 0 に戻るので、周期をまたいで連続する）"""

    def __init__(self, rate, amp):
        self.rate = rate
        self.amp = amp
        self.out = []
        self.t = 0.0  # 現在の時刻（サンプル単位、小数）

    def cycle(self, freq):
        period = self.rate / freq
        end = self.t + period
        n0 = math.ceil(self.t)
        n1 = math.ceil(end)
        for n in range(n0, n1):
            self.out.append(self.amp * math.sin(2 * math.pi * (n - self.t) / period))
        self.t = end

    def bits(self, bits):
        for b in bits:
            self.cycle(F1 if b else F0)

    def silence(self, sec):
        n0 = math.ceil(self.t)
        self.t += sec * self.rate
        self.out.extend([0.0] * (math.ceil(self.t) - n0))


def encode(app, version=1, passes=2, gap_ms=20.0, rate=48000, amp=0.99, product=PRODUCT_ID):
    """サンプル列（-1..1）を返す"""
    if not 0 < len(app) <= APP_MAX:
        raise ValueError(f"app size {len(app)} is out of range (1..{APP_MAX})")
    pages = [app[i:i + PAGE_SIZE] for i in range(0, len(app), PAGE_SIZE)]
    pages[-1] = pages[-1] + b"\xff" * (PAGE_SIZE - len(pages[-1]))
    s = Synth(rate, amp)
    gap = gap_ms / 1000

    def send(body):
        s.bits(packet_bits(body))
        s.silence(gap)

    s.silence(LEAD_S)
    for _ in range(passes):
        send(info_body(PKT_START, product, version, app))
        for i, page in enumerate(pages):
            send(bytes([PKT_DATA, i]) + page)
    send(info_body(PKT_END, product, version, app))
    s.silence(LEAD_S)
    return s.out


def factory_image(app, version=1, product=PRODUCT_ID):
    """工場用の 16KB イメージ（アプリ + 0xFF 埋め + メタデータページ）"""
    if not 0 < len(app) <= APP_MAX:
        raise ValueError(f"app size {len(app)} is out of range (1..{APP_MAX})")
    meta = info_body(PKT_END, product, version, app)[:PAGE_SIZE]
    return app + b"\xff" * (APP_MAX - len(app)) + meta


def write_wav(path, samples, rate):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)  # 左右同じ信号（TS ケーブルでどちらを拾ってもよい）
        w.setsampwidth(2)
        w.setframerate(rate)
        frames = bytearray()
        for v in samples:
            x = max(-32767, min(32767, int(round(v * 32767))))
            frames += struct.pack("<hh", x, x)
        w.writeframes(bytes(frames))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("bin")
    ap.add_argument("wav")
    ap.add_argument("--version", type=int, default=1, help="ファームのバージョン（0..65535）")
    ap.add_argument("--passes", type=int, default=2, help="全ページを送る回数")
    ap.add_argument("--gap-ms", type=float, default=20.0, help="パケットの後の無音（ms）")
    ap.add_argument("--rate", type=int, default=48000)
    ap.add_argument("--amp", type=float, default=0.99, help="振幅（1.0 = フルスケール）")
    ap.add_argument("--factory", help="工場用の 16KB イメージ（アプリ + メタデータ）の出力先")
    ap.add_argument("--product", type=lambda x: int(x, 0), default=PRODUCT_ID, help=argparse.SUPPRESS)
    a = ap.parse_args()

    with open(a.bin, "rb") as f:
        app = f.read()
    samples = encode(app, a.version, a.passes, a.gap_ms, a.rate, a.amp, a.product)
    write_wav(a.wav, samples, a.rate)
    if a.factory:
        with open(a.factory, "wb") as f:
            f.write(factory_image(app, a.version, a.product))
        print(f"{a.factory}: {APP_MAX + PAGE_SIZE} bytes (app + metadata, version {a.version})")
    pages = (len(app) + PAGE_SIZE - 1) // PAGE_SIZE
    print(f"{a.wav}: {len(app)} bytes, {pages} pages x {a.passes} passes, "
          f"{len(samples) / a.rate:.1f}s, crc32={zlib.crc32(app) & 0xFFFFFFFF:08x}")


if __name__ == "__main__":
    sys.exit(main())
