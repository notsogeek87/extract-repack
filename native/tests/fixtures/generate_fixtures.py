#!/usr/bin/env python3
"""Generates small synthetic ".arc" container fixtures for
native/tests/arc_reader_test.cpp, following the exact byte-level FreeArc
"ArC" container format verified against the real Unarc source in this
session (see docs/ANALYSIS.md §1bis / §6bis):

  - 1-9 byte VLE integers (see native/arc/vle.h for the bit layout table)
  - NUL-terminated strings
  - FOOTER located by scanning the last 4096 bytes from EOF for the
    4-byte signature, followed by CRC-32-validated fields
  - a DIRECTORY block describing one "store" (uncompressed) solid block
    containing two files

Re-run this script (`python3 generate_fixtures.py`) whenever the fixture
layout needs to change; the generated .arc files are committed so the C++
test suite does not need Python at build/test time.
"""
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
SIGNATURE = bytes([0x41, 0x72, 0x43, 0x01])  # "ArC" + 0x01

BLOCK_DIRECTORY = 3
BLOCK_FOOTER = 4


def write_vle(value: int) -> bytes:
    n = 1
    while n < 8 and value >= (1 << (7 * n)):
        n += 1
    if n == 8 and value >= (1 << 56):
        return b"\xff" + value.to_bytes(8, "little")
    flag = 0x7F if n == 8 else (1 << (n - 1)) - 1
    raw = (value << n) | flag
    return raw.to_bytes(n, "little")


def cstr(s: str) -> bytes:
    return s.encode("ascii") + b"\x00"


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def fixed4(value: int) -> bytes:
    return struct.pack("<I", value & 0xFFFFFFFF)


def fixed1(value: int) -> bytes:
    return bytes([value & 0xFF])


def build_directory_block(files, data_block_pos_relative_offset, data_block_len, compressor="store"):
    """files: list of (dir_name, file_name, content_bytes)."""
    dirs = []
    dir_index = {}
    for dir_name, _, _ in files:
        if dir_name not in dir_index:
            dir_index[dir_name] = len(dirs)
            dirs.append(dir_name)

    body = b""
    body += write_vle(1)  # num_of_blocks
    body += write_vle(len(files))  # num_of_files[0]
    body += cstr(compressor)  # compressors[0]
    body += write_vle(data_block_pos_relative_offset)  # offsets[0]
    body += write_vle(data_block_len)  # compsizes[0]

    body += write_vle(len(dirs))
    for d in dirs:
        body += cstr(d)

    for _, name, _ in files:
        body += cstr(name)
    for dir_name, _, _ in files:
        body += write_vle(dir_index[dir_name])
    for _, _, content in files:
        body += write_vle(len(content))
    for _ in files:
        body += fixed4(0)  # time
    for _ in files:
        body += fixed1(0)  # isdir
    for _, _, content in files:
        body += fixed4(crc32(content))
    return body


def build_footer(dir_block_type, dir_block_compressor, dir_block_relative_pos, dir_block_origsize, dir_block_compsize, dir_block_crc):
    body = b""
    body += write_vle(1)  # one control block described
    body += write_vle(dir_block_type)
    body += cstr(dir_block_compressor)
    body += write_vle(dir_block_relative_pos)
    body += write_vle(dir_block_origsize)
    body += write_vle(dir_block_compsize)
    body += fixed4(dir_block_crc)
    body += fixed1(0)  # arcLocked
    body += write_vle(0)  # comment length = 0
    return body


def build_footer_local_descriptor(footer_origsize, footer_compsize, footer_crc, compressor="store"):
    descriptor = b""
    descriptor += SIGNATURE
    descriptor += write_vle(BLOCK_FOOTER)
    descriptor += cstr(compressor)
    descriptor += write_vle(footer_origsize)
    descriptor += write_vle(footer_compsize)
    descriptor += fixed4(footer_crc)
    trailing_crc = fixed4(crc32(descriptor))
    return descriptor + trailing_crc


def assemble(files, dir_block_compressor_on_disk="store", footer_compressor_on_disk="store", corrupt_directory_byte=False):
    header_prefix = SIGNATURE  # cheap-detection bytes only; ArcReader::list() never reads these.

    data_block = b"".join(content for _, _, content in files)
    data_block_pos = len(header_prefix)

    dir_block_body = build_directory_block(files, data_block_len=len(data_block), data_block_pos_relative_offset=len(data_block))
    if corrupt_directory_byte:
        dir_block_body = bytearray(dir_block_body)
        dir_block_body[10] ^= 0xFF  # flips a byte inside the encoded metadata -> CRC mismatch on read.
        dir_block_body = bytes(dir_block_body)
    dir_block_pos = data_block_pos + len(data_block)
    dir_block_crc = crc32(dir_block_body)

    footer_content_pos = dir_block_pos + len(dir_block_body)
    footer_body = build_footer(
        dir_block_type=BLOCK_DIRECTORY,
        dir_block_compressor=dir_block_compressor_on_disk,
        dir_block_relative_pos=footer_content_pos - dir_block_pos,
        dir_block_origsize=len(dir_block_body),
        dir_block_compsize=len(dir_block_body),
        dir_block_crc=dir_block_crc,
    )
    footer_crc = crc32(footer_body)

    descriptor = build_footer_local_descriptor(
        footer_origsize=len(footer_body),
        footer_compsize=len(footer_body),
        footer_crc=footer_crc,
        compressor=footer_compressor_on_disk,
    )

    return header_prefix + data_block + dir_block_body + footer_body + descriptor


def main():
    files = [
        ("data", "hello.txt", b"Hello from inside a FreeArc-style solid block!\n"),
        ("", "readme.txt", b"This is the root readme.\n"),
    ]

    with open(os.path.join(HERE, "sample_store.arc"), "wb") as f:
        f.write(assemble(files))

    with open(os.path.join(HERE, "sample_unsupported_codec.arc"), "wb") as f:
        f.write(assemble(files, dir_block_compressor_on_disk="lzma2"))

    with open(os.path.join(HERE, "sample_corrupt.arc"), "wb") as f:
        f.write(assemble(files, corrupt_directory_byte=True))

    # Observed on a real 20 GB repack: the footer descriptor is followed by a
    # few more bytes rather than ending exactly at EOF, which makes the
    # reference's fixed "body = window - 4" split (openWithCRCAtEnd in the
    # vendored Unarc source) overshoot and CRC-fail a valid descriptor. The
    # reader locates the split by CRC instead, so this must still list.
    with open(os.path.join(HERE, "sample_trailing_bytes.arc"), "wb") as f:
        f.write(assemble(files) + b"\xde\xad\xbe\xef" * 5)

    # Only a coincidental "ArC\x01" near EOF and nothing that CRC-validates:
    # must be rejected, with diagnostics, rather than misparsed.
    with open(os.path.join(HERE, "not_an_arc_but_has_signature.bin"), "wb") as f:
        f.write(b"\x11" * 200 + SIGNATURE + b"\x22" * 40)

    # Inno Setup style multi-part external data: the single logical archive
    # (same bytes as sample_store.arc) split across three sibling files at
    # arbitrary byte boundaries, including mid-solid-block and mid-footer —
    # exactly how fg-01.bin/fg-02.bin/fg-03.bin split a FreeArc container in
    # a real repack. FileSource(paths) must present these as one stream.
    whole = assemble(files)
    cut1 = len(whole) // 3
    cut2 = (2 * len(whole)) // 3
    parts = [whole[:cut1], whole[cut1:cut2], whole[cut2:]]
    for i, part in enumerate(parts, start=1):
        with open(os.path.join(HERE, f"sample_multipart-{i:02d}.bin"), "wb") as f:
            f.write(part)

    with open(os.path.join(HERE, "not_an_archive.bin"), "wb") as f:
        f.write(b"This is just a plain text file, not an ArC archive at all.\n" * 3)

    print("Wrote fixtures to", HERE)


if __name__ == "__main__":
    main()
