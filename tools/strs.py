import re,sys
data=open(sys.argv[1],'rb').read()
pat=re.compile(sys.argv[2].encode(),re.I)
seen={}
for m in re.finditer(rb'[\x20-\x7e]{5,}',data):
    s=m.group()
    if pat.search(s): seen.setdefault(s,m.start())
for s,o in sorted(seen.items(),key=lambda x:x[1]): print(hex(o),s.decode())
# utf16
for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}',data):
    s=m.group().decode('utf-16le')
    if pat.search(s.encode()): print('W',hex(m.start()),s)
