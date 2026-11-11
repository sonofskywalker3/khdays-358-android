"""Writes zero.bin: the input file vkrun reads, with every buffer zero (no application data)."""
import struct

SIZES = [8716, 147456, 147456, 32768, 16384, 131072, 196608, 263168, 786432, 786432]
params = [0] * 36
params[0], params[1], params[2] = 2, 512, 384     # scale, width, height
params[24], params[25], params[26] = 2, 512, 384  # scale3d, width3d, height3d
params[29], params[30] = 512, 384                 # hud_width, hud_height
with open("zero.bin", "wb") as f:
    f.write(struct.pack("<4I", 0x4E49444B, 10, 144, 2))
    f.write(struct.pack("<10I", *SIZES))
    f.write(struct.pack("<36I", *params))
    f.write(bytes(sum(SIZES)))
