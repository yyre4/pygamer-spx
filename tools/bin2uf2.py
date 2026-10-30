#!/usr/bin/env python3
"""Minimal .bin -> .uf2 converter for the PyGamer (SAMD51, bootloader
base 0x4000, family 0x55114460). Standard UF2 block format."""
import struct, sys

UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END = 0x0AB16F30
FAMILY_SAMD51 = 0x55114460
BASE = 0x4000
PAYLOAD = 256

def convert(bin_path, uf2_path):
    data = open(bin_path, 'rb').read()
    nblocks = (len(data) + PAYLOAD - 1) // PAYLOAD
    out = bytearray()
    for i in range(nblocks):
        chunk = data[i * PAYLOAD:(i + 1) * PAYLOAD]
        block = struct.pack('<IIIIIIII',
                            UF2_MAGIC_START0, UF2_MAGIC_START1,
                            0x2000, BASE + i * PAYLOAD,
                            PAYLOAD, i, nblocks, FAMILY_SAMD51)
        block += chunk + b'\x00' * (476 - len(chunk))
        block += struct.pack('<I', UF2_MAGIC_END)
        assert len(block) == 512
        out += block
    open(uf2_path, 'wb').write(out)
    print(f"{uf2_path}: {len(out)} bytes, {nblocks} blocks, family 0x{FAMILY_SAMD51:08x}, base 0x{BASE:x}")

if __name__ == '__main__':
    convert(sys.argv[1], sys.argv[2])
