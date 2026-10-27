# per-channel RMS (dBFS) of a WAV written by loopback.exe (float or 16-bit PCM, WAVE_FORMAT_EXTENSIBLE ok)
import struct, sys, numpy as np
d = open(sys.argv[1], 'rb').read()
fmt = d.index(b'fmt ') + 8
tag, ch, rate, _, align, bits = struct.unpack_from('<HHIIHH', d, fmt)
if tag == 0xFFFE:
    tag = struct.unpack_from('<H', d, fmt + 24)[0]
    mask = struct.unpack_from('<I', d, fmt + 20)[0]
else:
    mask = 0
data = d[d.index(b'data') + 8:]
a = np.frombuffer(data, dtype=np.float32 if tag == 3 else np.int16).reshape(-1, ch).astype(np.float64)
if tag != 3: a /= 32768
names = ['FL', 'FR', 'FC', 'LFE', 'BL', 'BR', 'FLC', 'FRC', 'BC', 'SL', 'SR']
labels = [n for i, n in enumerate(names) if mask >> i & 1] if mask else ['ch%d' % i for i in range(ch)]
print('%d ch %d Hz, %.1f s' % (ch, rate, len(a) / rate))
for i in range(ch):
    rms = np.sqrt(np.mean(a[:, i] ** 2)) + 1e-12
    print('%-4s %6.1f dBFS' % (labels[i] if i < len(labels) else 'ch%d' % i, 20 * np.log10(rms)))
