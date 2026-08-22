"""Decode an FSK firmware WAV back to bytes and check it against the .bin.

Mirrors avr_audio_bootloader/fsk/encoder.py in reverse: each symbol is a run of
constant-sign samples whose length says which symbol it is (8 = 0, 16 = 1,
64 = inter-packet pause). A packet is 4 preamble bytes of 0x55, SPM_PAGESIZE
bytes of payload, then a big-endian CRC32 of that payload - which is exactly what
grids/bootloader/bootloader.cc checks before it writes a flash page.
"""
import wave, numpy as np, zlib, sys

# Symbol durations in samples, straight from avr_audio_bootloader/fsk/decoder.h -
# kZero/kOne/kPause. These are NOT the encoder's defaults (8/16/64); using those
# produces a file where every zero reads as a one and every one reads as a pause,
# so nothing ever CRCs. Verified against Mutable's own grids_1.0.wav.
ZERO, ONE, PAUSE, PAGE = 4, 8, 16, 128

def symbols(path):
    w = wave.open(path); n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype='<i2').astype(np.int8.__mro__[0])
    x = np.frombuffer(wave.open(path).readframes(n), dtype='<i2')
    sign = np.sign(x)
    nz = sign != 0
    idx = np.flatnonzero(nz)
    if len(idx) == 0: return []
    s = sign[idx[0]:idx[-1]+1]
    change = np.flatnonzero(np.diff(s)) + 1
    bounds = np.concatenate(([0], change, [len(s)]))
    return np.diff(bounds)

def decode(path):
    runs = symbols(path)
    bits, packets, cur = [], [], []
    for r in runs:
        if r == ZERO:   cur.append(0)
        elif r == ONE:  cur.append(1)
        else:
            if cur: packets.append(cur); cur = []
    if cur: packets.append(cur)
    out, bad = bytearray(), 0
    for p in packets:
        if len(p) % 8: bad += 1; continue
        by = bytes(int(''.join(map(str, p[i:i+8])), 2) for i in range(0, len(p), 8))
        if len(by) != 4 + PAGE + 4 or by[:4] != b'\x55\x55\x55\x55': bad += 1; continue
        payload, crc = by[4:4+PAGE], int.from_bytes(by[4+PAGE:], 'big')
        if zlib.crc32(payload) & 0xffffffff != crc: bad += 1; continue
        out += payload
    return out, len(packets), bad

for bank in ['jazzlatin', 'groove', 'club']:
    data, npkt, bad = decode(f'grids_{bank}.wav')
    ref = open(f'grids_{bank}.bin', 'rb').read()
    padded = ref + b'\xff' * ((-len(ref)) % PAGE)
    ok = data[:len(padded)] == padded and bad == 0
    print(f"{bank:10} packets {npkt:>3}  bad {bad}  recovered {len(data):>6}B  "
          f"payload matches firmware: {ok}")
