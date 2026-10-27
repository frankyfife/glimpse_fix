# set (or clear with --off) the LARGE_ADDRESS_AWARE flag of a PE file in place
import struct, sys
path = sys.argv[1]
with open(path, 'r+b') as f:
    d = bytearray(f.read(0x2000))
    pe = struct.unpack_from('<I', d, 0x3c)[0]
    ch = struct.unpack_from('<H', d, pe + 0x16)[0]
    new = ch & ~0x20 if '--off' in sys.argv else ch | 0x20
    f.seek(pe + 0x16)
    f.write(struct.pack('<H', new))
    print('characteristics %#x -> %#x' % (ch, new))
