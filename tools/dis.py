import sys, capstone
img = open(sys.argv[1], 'rb').read(); base = 0x400000
a = int(sys.argv[2], 16); n = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x80
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
for i in md.disasm(img[a - base:a - base + n], a):
    print('%08x  %-20s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
