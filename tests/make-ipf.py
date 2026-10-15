#!/usr/bin/env python3
"""Writes an IPF disk image of an Amiga disk (.adf), for the PUAE core's
tests. IPF is the format the Software Preservation Society keeps disks in;
the core reads it with waterbox/ipf-reader.c. No IPF of anyone's is needed
for the tests: this script makes one from the test program's disk.

usage: make-ipf.py <in.adf> <out.ipf> [weak=<track>:<sector>:<bytes>]
                   [copylock=<track>] [expect=<out.expect>]

The IPF holds each track as the SPS encoder writes an AmigaDOS one: eleven
blocks, one a sector, each the sector's bytes as MFM-decoded data with its
two sync words as cells, and the gap at the end of the last block. Tracks
are numbered cylinder * 2 + head.

  weak=T:S:N   the first N bytes of sector S's data on track T are weak
               bits: they read differently on every revolution (that
               sector's checksum then fails, as copy protection wants)
  copylock=T   track T has the cell lengths of Rob Northen's Copylock
               (density type 3): blocks 4, 5 and 6 are 5.5% fast, 0.5%
               fast and 4.5% slow
  expect=F     also writes, for tests/ipf-check.c's answers to be checked
               against: each track's cells as one byte a cell ('0', '1',
               or 'w' for a weak one), after a line with the track's
               number, its count of cells and its density type
"""
import struct
import sys

SECTORS = 11
GAP_CELLS = 4496         # the gap after the last sector: a track of 100,240 cells, as the
                         # IPFs of AmigaDOS disks have (one revolution at 2 microseconds a cell)
SECTOR_CELLS = 1088 * 8  # a sector as written: 1,088 bytes of cells


def crc32r(data):
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if crc & 1 else crc >> 1
    return crc ^ 0xFFFFFFFF


def u32(*v):
    return b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in v)


def record(tag, body):
    """An IPF record: its name, its size, its CRC (taken with the CRC's own
    place as zeros), then the rest."""
    head = tag + u32(12 + len(body), 0) + body
    crc = crc32r(head)
    return head[:8] + u32(crc) + head[12:]


def split(longs):
    """AmigaDOS writes a run of long words as all their odd bits, then all
    their even bits; each half is 16 data bits a long word. Returns the
    data bits as bytes, as MFM decoding gives them."""
    def bits(v, odd):
        d = 0
        for i in range(16):
            d = (d << 1) | ((v >> (31 - 2 * i - (0 if odd else 1))) & 1)
        return d
    out = b""
    for odd in (True, False):
        for v in longs:
            out += struct.pack(">H", bits(v, odd))
    return out


def checksum(data_bytes):
    """AmigaDOS's checksum: the long words of the MFM cells XORed, data bits
    only. Over the data bits a long word of cells holds 16 of them; built
    back into the cell positions they sit at."""
    c = 0
    for i in range(0, len(data_bytes), 2):
        d = struct.unpack(">H", data_bytes[i:i + 2])[0]
        cells = 0
        for b in range(16):
            cells |= ((d >> (15 - b)) & 1) << (30 - 2 * b)
        c ^= cells
    return c & 0x55555555


def sector_stream(track, sector, data):
    """The sector's bytes after its sync words, MFM-decoded."""
    longs = struct.unpack(">128I", data)
    info = (0xFF << 24) | (track << 16) | (sector << 8) | (SECTORS - sector)
    head = split([info]) + split([0, 0, 0, 0])
    body = split(longs)
    return head + split([checksum(head)]) + split([checksum(body)]) + body


def element(kind, count, payload=b""):
    """One element of a block's data stream: a byte whose top three bits say
    how many bytes the count takes and whose low five bits say its kind,
    the count, then the bytes."""
    n = 1 if count < 0x100 else 2 if count < 0x10000 else 3
    return bytes([(n << 5) | kind]) + count.to_bytes(n, "big") + payload


def mfm(decoded, prev):
    """MFM cells of decoded bytes: each data bit gets a clock bit before it,
    1 only between two 0 data bits. Returns the cells and the last bit."""
    out = []
    for byte in decoded:
        for i in range(8):
            bit = (byte >> (7 - i)) & 1
            out.append(1 if not prev and not bit else 0)
            out.append(bit)
            prev = bit
    return out, prev


def main():
    src, dst = sys.argv[1], sys.argv[2]
    weak = None
    copylock = None
    expect = None
    for a in sys.argv[3:]:
        k, _, v = a.partition("=")
        if k == "weak":
            weak = tuple(int(x) for x in v.split(":"))
        elif k == "copylock":
            copylock = int(v)
        elif k == "expect":
            expect = v
        else:
            sys.exit("unknown option " + a)
    adf = open(src, "rb").read()
    if len(adf) != 901120:
        sys.exit("an .adf of 901,120 bytes is wanted")

    out = record(b"CAPS", b"")
    # INFO: a floppy disk (1), the SPS encoder (2) at its revision 1, release
    # and revision 1, cylinders 0 to 79, heads 0 and 1, made on the first of
    # January 2026, for the Amiga (platform 1)
    out += record(b"INFO", u32(1, 2, 1, 1, 1, 0, 0, 79, 0, 1, 20260101, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0))

    imges, datas, lines = b"", b"", []
    for track in range(160):
        cyl, head = track // 2, track % 2
        density = 3 if track == copylock else 2
        streams, cells, prev = [], [], False
        weak_cells = 0
        for s in range(SECTORS):
            data = adf[(track * SECTORS + s) * 512:(track * SECTORS + s + 1) * 512]
            dec = sector_stream(track, s, data)
            st = element(2, 2, b"\0\0") + element(1, 4, b"\x44\x89\x44\x89")
            c, prev = mfm(b"\0\0", prev)
            cells += c
            sync = [int(b) for b in "01000100100010010100010010001001"]
            cells += sync
            prev = True
            if weak and weak[0] == track and weak[1] == s:
                lead = 4 + 16 + 4 + 4          # info, label, the two checksums
                n = weak[2]
                st += element(2, lead, dec[:lead]) + element(5, n) + element(2, len(dec) - lead - n, dec[lead + n:])
                c, prev = mfm(dec[:lead], prev)
                cells += c
                cells += ["w"] * (n * 16)
                weak_cells += n * 16
                prev = False                    # MAME's reader: a weak run leaves no last bit
                c, prev = mfm(dec[lead + n:], prev)
                cells += c
            else:
                st += element(2, len(dec), dec)
                c, prev = mfm(dec, prev)
                cells += c
            streams.append(st + b"\0")
        gap_cells = GAP_CELLS
        # the gap: decoded zeros (cells 1010...), after the last sector
        gap = []
        for i in range(gap_cells // 2):
            bit = 0
            gap.append(1 if not prev and not bit else 0)
            gap.append(bit)
            prev = bit
        # MAME's reader puts the write splice in the middle of the gap and
        # turns three cells around there, as a drive's write leaves it
        splice_at = len(cells) + gap_cells // 2
        cells += gap
        for i in range(3):
            v = cells[splice_at + i]
            cells[splice_at + i] = 1 - v if v != "w" else v

        # the blocks: 32 bytes each, then the streams
        blocks, offset = b"", 32 * SECTORS
        for s, st in enumerate(streams):
            g = gap_cells if s == SECTORS - 1 else 0
            # data cells, gap cells, data bytes (gap type 0), gap bytes, MFM
            # (1), flags (gap type 0, sizes in bytes), the gap's pattern (a
            # zero byte), where the stream is
            blocks += u32(SECTOR_CELLS, g, SECTOR_CELLS // 16, g // 16, 1, 0, 0, offset)
            offset += len(st)
        extra = blocks + b"".join(streams)
        size_cells = SECTORS * SECTOR_CELLS + gap_cells
        imges += record(b"IMGE", u32(cyl, head, density, 1, size_cells // 8, 0, 0,
                                     SECTORS * SECTOR_CELLS, gap_cells, size_cells,
                                     SECTORS, 0, weak_cells, track, 0, 0, 0))
        datas += record(b"DATA", u32(len(extra), len(extra) * 8, crc32r(extra), track)) + extra
        lines.append((track, density, cells))

    out += imges + datas
    open(dst, "wb").write(out)
    if expect:
        with open(expect, "wb") as f:
            for track, density, cells in lines:
                f.write(b"%d %d %d\n" % (track, len(cells), density))
                f.write(bytes(ord(str(c)) for c in cells))
                f.write(b"\n")


if __name__ == "__main__":
    main()
