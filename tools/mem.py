# read/write memory of the running game: mem.py r <addr> <len> | mem.py w <addr> <hexbytes> | mem.py s <addr> (read C string)
import ctypes, subprocess, sys
k32 = ctypes.WinDLL('kernel32')
pid = [int(l.split('","')[1]) for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh']).decode('mbcs').splitlines() if l.lower().startswith('"splitsecond.exe"')][0]
h = k32.OpenProcess(0x1F0FFF, False, pid)
def rd(a, n):
    b = ctypes.create_string_buffer(n); g = ctypes.c_size_t()
    k32.ReadProcessMemory(h, ctypes.c_void_p(a), b, n, ctypes.byref(g)); return b.raw[:g.value]
op, a = sys.argv[1], int(sys.argv[2], 16)
if op == 'r': print(rd(a, int(sys.argv[3], 16)).hex())
elif op == 's': print(rd(a, 128).split(b'\0')[0])
elif op == 'w':
    d = bytes.fromhex(sys.argv[3]); g = ctypes.c_size_t()
    print(k32.WriteProcessMemory(h, ctypes.c_void_p(a), d, len(d), ctypes.byref(g)), g.value)
