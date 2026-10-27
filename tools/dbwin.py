# Minimal OutputDebugString listener (DBWIN protocol); prints "pid: text" lines
import ctypes, ctypes.wintypes as wt, sys, time
k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileMappingW.restype = wt.HANDLE
k32.MapViewOfFile.restype = ctypes.c_void_p
k32.CreateEventW.restype = wt.HANDLE
buf_ready = k32.CreateEventW(None, False, False, "DBWIN_BUFFER_READY")
data_ready = k32.CreateEventW(None, False, False, "DBWIN_DATA_READY")
mapping = k32.CreateFileMappingW(wt.HANDLE(-1), None, 4, 0, 4096, "DBWIN_BUFFER")
view = k32.MapViewOfFile(mapping, 4, 0, 0, 4096)
end = time.time() + float(sys.argv[1]) if len(sys.argv) > 1 else 1e18
k32.SetEvent(buf_ready)
while time.time() < end:
    if k32.WaitForSingleObject(data_ready, 500) == 0:
        pid = ctypes.c_uint32.from_address(view).value
        text = ctypes.string_at(view + 4).decode('latin1', 'replace').rstrip()
        print(pid, text, flush=True)
        k32.SetEvent(buf_ready)
