import sys, struct, re, collections
img = open(sys.argv[1], 'rb').read(); base = 0x400000
c = collections.Counter()
for m in re.finditer(rb'\xff\x15(....)', img[:0x79b000], re.S):
    t = struct.unpack('<I', m.group(1))[0]
    c[t >> 16] += 1
for k, v in c.most_common(15): print(hex(k << 16), v)
c2 = collections.Counter()
for m in re.finditer(rb'\xe8(....)', img[:0x79b000], re.S):
    t = (struct.unpack('<i', m.group(1))[0] + base + m.start() + 5) & 0xffffffff
    if t >= 0xe1b000 + base - 0x400000 + 0x400000 and t < 0x2000000: c2[t >> 20] += 1
for k, v in c2.most_common(10): print('E8->', hex(k << 20), v)
