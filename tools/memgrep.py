# grep a running process's committed memory for a byte pattern; print context strings
import ctypes, subprocess, sys, re
k32 = ctypes.WinDLL('kernel32')
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', ctypes.c_ulong), ('PartitionId', ctypes.c_ushort), ('RegionSize', ctypes.c_size_t), ('State', ctypes.c_ulong), ('Protect', ctypes.c_ulong), ('Type', ctypes.c_ulong)]
name, pat = sys.argv[1], sys.argv[2].encode()
ctx = int(sys.argv[3]) if len(sys.argv) > 3 else 64
maxhits = int(sys.argv[4]) if len(sys.argv) > 4 else 20
pid = [int(l.split('","')[1]) for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh']).decode('mbcs').splitlines() if l.lower().startswith('"%s"' % name.lower())][0]
h = k32.OpenProcess(0x0410, False, pid)
a, m, hits = 0, MBI(), 0
while a < 0x7fff0000 and hits < maxhits:
    if not k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)): break
    if m.State == 0x1000 and not (m.Protect & 0x101) and m.Type != 0x1000000:
        buf = ctypes.create_string_buffer(m.RegionSize); got = ctypes.c_size_t()
        if k32.ReadProcessMemory(h, ctypes.c_void_p(a), buf, m.RegionSize, ctypes.byref(got)):
            d = buf.raw[:got.value]
            for x in re.finditer(re.escape(pat), d):
                s = d[max(0, x.start() - ctx):x.end() + ctx]
                print(hex(a + x.start()), re.sub(rb'[^\x20-\x7e]', b'.', s).decode())
                hits += 1
                if hits >= maxhits: break
    a += m.RegionSize
