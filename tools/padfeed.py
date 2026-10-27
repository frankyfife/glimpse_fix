# Test feed for the dinput8 proxy (TestFeed=1): owns the shared memory and plays commands
# appended to <cmdfile>, one per line:
#   connect <slot> <0|1>
#   press <button> [ms]          buttons: A B X Y LB RB BACK START LS RS UP DOWN LEFT RIGHT
#   hold <button> | release <button>
#   stick <L|R> <x> <y>          -1.0 .. 1.0
#   trigger <L|R> <0..1>
#   wait <ms>
#   shot <n>                     save the next frame as shots\ig<n>.jpg
# Status (rumble, glyph mode) is written to <statusfile> twice a second.
import ctypes, ctypes.wintypes as wt, sys, time, os

BUTTONS = {'UP': 0x1, 'DOWN': 0x2, 'LEFT': 0x4, 'RIGHT': 0x8, 'START': 0x10, 'BACK': 0x20, 'LS': 0x40, 'RS': 0x80,
           'LB': 0x100, 'RB': 0x200, 'A': 0x1000, 'B': 0x2000, 'X': 0x4000, 'Y': 0x8000}

class Gamepad(ctypes.Structure):
    _fields_ = [('buttons', ctypes.c_ushort), ('lt', ctypes.c_ubyte), ('rt', ctypes.c_ubyte),
                ('lx', ctypes.c_short), ('ly', ctypes.c_short), ('rx', ctypes.c_short), ('ry', ctypes.c_short)]

class State(ctypes.Structure):
    _fields_ = [('packet', ctypes.c_uint32), ('pad', Gamepad)]

class Vib(ctypes.Structure):
    _fields_ = [('left', ctypes.c_ushort), ('right', ctypes.c_ushort)]

class Feed(ctypes.Structure):
    _fields_ = [('connected', ctypes.c_uint32 * 4), ('state', State * 4), ('rumble', Vib * 4), ('glyphsPad', ctypes.c_uint32),
                ('shotRequest', ctypes.c_uint32), ('fps', ctypes.c_float)]

k32 = ctypes.WinDLL('kernel32', use_last_error=True)
k32.CreateFileMappingW.restype = wt.HANDLE
k32.MapViewOfFile.restype = ctypes.c_void_p
h = k32.CreateFileMappingW(wt.HANDLE(-1), None, 4, 0, ctypes.sizeof(Feed), "Local\\SplitSecondPadTestFeed")
feed = Feed.from_address(k32.MapViewOfFile(h, 0xF001F, 0, 0, ctypes.sizeof(Feed)))
cmdfile, statusfile = sys.argv[1], sys.argv[2]
open(cmdfile, 'w').close()
pos = 0
queue = []
busy_until = 0.0
last_status = 0.0

def axis(v):
    return max(-32768, min(32767, int(float(v) * 32767)))

def bump(slot=0):
    feed.state[slot].packet += 1

while True:
    with open(cmdfile, 'r') as f:
        f.seek(pos)
        new = f.read()
        pos = f.tell()
    queue += [l.split() for l in new.splitlines() if l.strip()]
    now = time.time()
    while queue and now >= busy_until:
        c = queue.pop(0)
        op = c[0].lower()
        p = feed.state[0].pad
        if op == 'quit':
            sys.exit(0)
        elif op == 'connect':
            feed.connected[int(c[1])] = int(c[2])
        elif op == 'press':
            p.buttons |= BUTTONS[c[1].upper()]
            bump()
            queue.insert(0, ['release', c[1]])
            busy_until = now + (int(c[2]) if len(c) > 2 else 120) / 1000
            queue.insert(1, ['wait', '150'])
        elif op == 'hold':
            p.buttons |= BUTTONS[c[1].upper()]
            bump()
        elif op == 'release':
            p.buttons &= ~BUTTONS[c[1].upper()]
            bump()
        elif op == 'stick':
            if c[1].upper() == 'L':
                p.lx, p.ly = axis(c[2]), axis(c[3])
            else:
                p.rx, p.ry = axis(c[2]), axis(c[3])
            bump()
        elif op == 'trigger':
            v = max(0, min(255, int(float(c[2]) * 255)))
            if c[1].upper() == 'L':
                p.lt = v
            else:
                p.rt = v
            bump()
        elif op == 'shot':
            feed.shotRequest = int(c[1])
        elif op == 'wait':
            busy_until = now + int(c[1]) / 1000
        print(' '.join(c), flush=True)
    if now - last_status > 0.5:
        last_status = now
        r = feed.rumble[0]
        with open(statusfile, 'w') as f:
            f.write('rumble %d %d glyphsPad %d buttons %04x fps %.1f\n' % (r.left, r.right, feed.glyphsPad, feed.state[0].pad.buttons, feed.fps))
    time.sleep(0.01)
