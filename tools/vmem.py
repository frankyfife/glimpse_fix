# address space usage of the running game (committed / reserved / largest free block below 4 GB)
import ctypes, subprocess
k32 = ctypes.WinDLL('kernel32')
class MBI(ctypes.Structure):
    _fields_ = [('BaseAddress', ctypes.c_void_p), ('AllocationBase', ctypes.c_void_p), ('AllocationProtect', ctypes.c_ulong), ('PartitionId', ctypes.c_ushort), ('RegionSize', ctypes.c_size_t), ('State', ctypes.c_ulong), ('Protect', ctypes.c_ulong), ('Type', ctypes.c_ulong)]
pid = [int(l.split('","')[1]) for l in subprocess.check_output(['tasklist', '/fo', 'csv', '/nh']).decode('mbcs').splitlines() if l.lower().startswith('"splitsecond.exe"')][0]
h = k32.OpenProcess(0x0410, False, pid)
a, m = 0, MBI()
commit = reserve = free = maxfree = 0
while a < 0x100000000:
    if not k32.VirtualQueryEx(h, ctypes.c_void_p(a), ctypes.byref(m), ctypes.sizeof(m)): break
    if m.State == 0x1000: commit += m.RegionSize
    elif m.State == 0x2000: reserve += m.RegionSize
    elif a < 0x80000000 or True:
        free += m.RegionSize; maxfree = max(maxfree, m.RegionSize) if a < 0x80000000 else maxfree
    a += m.RegionSize
    if a >= 0x80000000: break
print('below 2GB: committed %d MB, reserved %d MB, free %d MB, largest free %d MB' % (commit >> 20, reserve >> 20, free >> 20, maxfree >> 20))
