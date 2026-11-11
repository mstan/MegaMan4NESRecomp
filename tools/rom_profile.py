"""Validate the supported original USA cartridge payload before generation."""
from pathlib import Path
import hashlib
import sys


def main():
    data = Path(sys.argv[1]).read_bytes()
    if len(data) < 16 or data[:4] != b'NES\x1a':
        raise SystemExit('An iNES Mega Man 4 (USA) ROM is required')
    start = 16 + (512 if data[6] & 4 else 0)
    size = data[4] * 16384 + data[5] * 8192
    mapper = (data[6] >> 4) | (data[7] & 0xf0)
    if mapper != 4 or len(data) != start + size:
        raise SystemExit('Mega Man 4 requires the original MMC3 cartridge layout')
    if hashlib.sha1(data[start:]).hexdigest() != '673a888c468abe33fb0b3f063c6837dd21d5d32b':
        raise SystemExit('Unsupported cartridge payload; supply Mega Man 4 (USA)')
    print('NTSC;usa')


if __name__ == '__main__':
    main()
