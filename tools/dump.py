# Dump SplitSecond.exe image from a running process into a PE usable by Ghidra (raw == virtual layout)
import ctypes, ctypes.wintypes as wt, sys, struct, subprocess
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
psapi = ctypes.WinDLL('psapi')
name = sys.argv[1] if len(sys.argv) > 1 else 'SplitSecond.exe'
out = sys.argv[2] if len(sys.argv) > 2 else 'SplitSecond_dump.exe'
pid = None
for line in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh']).decode('mbcs').splitlines():
    f = [x.strip('"') for x in line.split('","')]
    if f[0].lower() == name.lower(): pid = int(f[1])
if not pid: sys.exit('process not found')
h = k32.OpenProcess(0x0410, False, pid)
base = 0x400000
def rd(addr, n):
    buf = ctypes.create_string_buffer(n); got = ctypes.c_size_t()
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, n, ctypes.byref(got))
    return buf.raw[:got.value] if ok else None
hdr = rd(base, 0x1000)
pe = struct.unpack_from('<I', hdr, 0x3c)[0]
nsec = struct.unpack_from('<H', hdr, pe + 6)[0]
opt = pe + 24
size = struct.unpack_from('<I', hdr, opt + 56)[0]
img = bytearray(size)
for off in range(0, size, 0x1000):
    d = rd(base + off, 0x1000)
    if d: img[off:off + len(d)] = d
sh = opt + struct.unpack_from('<H', hdr, pe + 20)[0]
for i in range(nsec):
    o = sh + i * 40
    vs, va = struct.unpack_from('<II', img, o + 8)
    struct.pack_into('<II', img, o + 16, (vs + 0xfff) & ~0xfff, va)  # SizeOfRawData, PointerToRawData
open(out, 'wb').write(img)
print('pid', pid, 'dumped', hex(size), 'to', out)
