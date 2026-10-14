#!/usr/bin/env python3
"""Writes a floppy disk image (.adf, 901,120 bytes) whose boot block is a
small test program for the PUAE core's tests. It needs no file of anyone's:
it boots with the Kickstart PUAE carries (AROS's) as well as with
Commodore's.

usage: make-amiga-disk.py <out.adf> [nopad]

The program, hand-assembled below (68000 machine code), runs from the boot
block, which the Kickstart loads and calls with the drive's I/O request in
A1 and the system in A6. It:
  - writes the boot block's first 512 bytes to sector 1700 of the disk
    through the drive (trackdisk.device), and stops the motor;
  - takes the machine over (no interrupts, no DMA: the screen is the
    background colour);
  - for ever, once a frame (it waits for the vertical blank):
    - counts the frames at 0x7E00C (a long word);
    - copies JOY1DAT (port 2's joystick) to 0x7E000, JOY0DAT (port 1's
      mouse counters or joystick) to 0x7E002, CIA-A's port A (the two fire
      buttons, bits 6 and 7, 0 while held) to 0x7E004 and POTGOR (the
      second buttons, bits 10 and 14, 0 while held) to 0x7E006;
    - takes a key from the keyboard when there is one: its code to
      0x7E008 (the key's code, bit 7 set when it went up) and one more to
      the count at 0x7E00A, and answers the keyboard as the machine must;
    - makes the background colour the frame count plus both ports' words.
Made with "nopad" it reads none of the controls (only the clock), so every
frame is a lag frame.
"""
import struct
import sys

BASE = 0x7E000


def w(v):
    return struct.pack(">H", v & 0xFFFF)


def l(v):
    return struct.pack(">I", v & 0xFFFFFFFF)


def program(nopad):
    code = bytearray()
    # ---- the disk write, through the drive's request (A1), A6 = exec
    code += w(0x2A49)                          # movea.l a1,a5
    code += w(0x337C) + w(3) + w(28)           # move.w #CMD_WRITE,io_Command(a1)
    code += w(0x237C) + l(512) + w(36)         # move.l #512,io_Length(a1)
    here = 12 + len(code) + 2                  # the extension word's address in the block
    code += w(0x41FA) + w(0 - here)            # lea start(pc),a0   (the boot block's first byte)
    code += w(0x2348) + w(40)                  # move.l a0,io_Data(a1)
    code += w(0x237C) + l(1700 * 512) + w(44)  # move.l #sector*512,io_Offset(a1)
    code += w(0x4EAE) + w(-456)                # jsr _LVODoIO(a6)
    code += w(0x224D)                          # movea.l a5,a1
    code += w(0x337C) + w(4) + w(28)           # move.w #CMD_UPDATE,io_Command(a1)
    code += w(0x4EAE) + w(-456)                # jsr _LVODoIO(a6)
    code += w(0x224D)                          # movea.l a5,a1
    code += w(0x337C) + w(9) + w(28)           # move.w #TD_MOTOR,io_Command(a1)
    code += w(0x42A9) + w(36)                  # clr.l io_Length(a1)   (motor off)
    code += w(0x4EAE) + w(-456)                # jsr _LVODoIO(a6)
    # ---- the machine is ours
    code += w(0x49F9) + l(0xDFF000)            # lea $dff000,a4
    code += w(0x47F9) + l(0xBFE001)            # lea $bfe001,a3   (CIA-A port A)
    code += w(0x397C) + w(0x7FFF) + w(0x9A)    # move.w #$7fff,INTENA(a4)
    code += w(0x397C) + w(0x7FFF) + w(0x9C)    # move.w #$7fff,INTREQ(a4)
    code += w(0x397C) + w(0x7FFF) + w(0x96)    # move.w #$7fff,DMACON(a4)
    code += w(0x397C) + w(0xFF00) + w(0x34)    # move.w #$ff00,POTGO(a4)  (second buttons readable)
    code += w(0x1039) + l(0xBFED01)            # move.b $bfed01,d0  (clear CIA-A's interrupt flags)
    code += w(0x7E00)                          # moveq #0,d7
    for addr in range(BASE, BASE + 16, 2):
        code += w(0x33FC) + w(0) + l(addr)     # move.w #0,addr
    loop = len(code)
    # wait for the vertical blank, and clear it
    code += w(0x082C) + w(5) + w(0x1F)         # btst #5,INTREQR+1(a4)
    code += w(0x67F8)                          # beq.s back to the btst
    code += w(0x397C) + w(0x0020) + w(0x9C)    # move.w #$20,INTREQ(a4)
    code += w(0x5287)                          # addq.l #1,d7
    code += w(0x23C7) + l(BASE + 0x0C)         # move.l d7,count
    if not nopad:
        code += w(0x33EC) + w(0x0C) + l(BASE + 0)   # move.w JOY1DAT(a4),$7e000
        code += w(0x33EC) + w(0x0A) + l(BASE + 2)   # move.w JOY0DAT(a4),$7e002
        code += w(0x13D3) + l(BASE + 4)             # move.b (a3),$7e004
        code += w(0x33EC) + w(0x16) + l(BASE + 6)   # move.w POTGOR(a4),$7e006
        # a key: CIA-A's serial port flag (bit 3 of its interrupt flags)
        key = len(code)
        code += w(0x1039) + l(0xBFED01)        # move.b $bfed01,d0
        code += w(0x0800) + w(3)               # btst #3,d0
        skip_at = len(code)
        code += w(0x6700)                      # beq.s nokey (patched below)
        code += w(0x1239) + l(0xBFEC01)        # move.b $bfec01,d1  (the key, as sent)
        code += w(0x4601)                      # not.b d1
        code += w(0xE219)                      # ror.b #1,d1
        code += w(0x13C1) + l(BASE + 8)        # move.b d1,$7e008
        code += w(0x5279) + l(BASE + 0x0A)     # addq.w #1,$7e00a
        # the answer: the serial line low for a while, then back
        code += w(0x08F9) + w(6) + l(0xBFEE01)  # bset #6,$bfee01  (CRA: serial port out)
        code += w(0x343C) + w(400)             # move.w #400,d2
        code += w(0x51CA) + w(0xFFFE)          # dbra d2,*
        code += w(0x08B9) + w(6) + l(0xBFEE01)  # bclr #6,$bfee01
        nokey = len(code)
        code[skip_at + 1] = (nokey - (skip_at + 2)) & 0xFF
        # the colour: the count plus both ports
        code += w(0x3007)                      # move.w d7,d0
        code += w(0xD06C) + w(0x0C)            # add.w JOY1DAT(a4),d0
        code += w(0xD06C) + w(0x0A)            # add.w JOY0DAT(a4),d0
    else:
        code += w(0x3007)                      # move.w d7,d0
    code += w(0x3940) + w(0x180)               # move.w d0,COLOR00(a4)
    back = loop - (len(code) + 2)
    code += w(0x6000) + w(back)                # bra.w loop
    return bytes(code)


def checksum(block):
    s = 0
    for i in range(0, 1024, 4):
        if i == 4:
            continue
        s += struct.unpack(">I", block[i:i + 4])[0]
        if s > 0xFFFFFFFF:
            s = (s & 0xFFFFFFFF) + 1
    return (~s) & 0xFFFFFFFF


def main():
    out = sys.argv[1]
    nopad = len(sys.argv) > 2 and sys.argv[2] == "nopad"
    code = program(nopad)
    assert 12 + len(code) <= 1024, len(code)
    block = bytearray(1024)
    block[0:4] = b"DOS\0"
    block[8:12] = l(880)
    block[12:12 + len(code)] = code
    block[4:8] = l(checksum(block))
    disk = bytearray(901120)
    disk[0:1024] = block
    open(out, "wb").write(disk)
    print("%s: a boot block of %d bytes of code%s" % (out, len(code), " (no controls read)" if nopad else ""))


if __name__ == "__main__":
    main()
