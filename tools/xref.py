# find code refs to an absolute address (imm32 / disp32) in the dump, print disasm context
import sys, struct, re, capstone
img = open(sys.argv[1], 'rb').read(); base = 0x400000
tgt = int(sys.argv[2], 16); ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 0
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
pat = struct.pack('<I', tgt)
TEXT_END = 0x79b000
for m in re.finditer(re.escape(pat), img[:TEXT_END]):
    o = m.start()
    # try instruction starts in the 6 bytes before
    for back in range(1, 8):
        s = o - back
        ins = list(md.disasm(img[s:s + 16], base + s, 1))
        if ins and ins[0].size > back and s + ins[0].size >= o + 4 and pat in ins[0].bytes:
            print(hex(ins[0].address), ins[0].mnemonic, ins[0].op_str)
            if ctx:
                for i in md.disasm(img[s - ctx:s + ctx * 2], base + s - ctx): pass
            break
