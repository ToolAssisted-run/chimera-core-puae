#!/usr/bin/env python3
"""Writes an audio CD for the PUAE core's tests: <folder>/tone.cue and
<folder>/tone.bin, one audio track of 20 seconds, a tone of 440 Hz on the
left and 660 Hz on the right (CD audio: 16-bit little-endian stereo samples
at 44.1 kHz, 2,352 bytes a sector). It needs no file of anyone's.

usage: make-audio-cd.py <folder>

The CD32's start-up screen shows a CD with no program on it in its CD
player, which plays the track when the pad's red button is pressed: the
whole path of CD audio, from the drive's thread to the sound."""
import math
import os
import struct
import sys


def main():
    out = sys.argv[1]
    n = 44100 * 20
    n -= n % 588  # whole sectors
    b = bytearray()
    for i in range(n):
        left = int(12000 * math.sin(2 * math.pi * 440 * i / 44100))
        right = int(12000 * math.sin(2 * math.pi * 660 * i / 44100))
        b += struct.pack("<hh", left, right)
    open(os.path.join(out, "tone.bin"), "wb").write(b)
    open(os.path.join(out, "tone.cue"), "w").write('FILE "tone.bin" BINARY\n  TRACK 01 AUDIO\n    INDEX 01 00:00:00\n')
    print("%s: tone.cue, one audio track of %d sectors" % (out, len(b) // 2352))


if __name__ == "__main__":
    main()
