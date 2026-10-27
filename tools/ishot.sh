#!/bin/sh
# in-game capture n (TestFeed=1, Log=1) -> shots/ig<n>.jpg and shots/ig<n>_s.png (1/3 size)
# usage: tools/ishot.sh <n>    (GAME_DIR overrides the default Steam path)
P=$(cd "$(dirname "$0")/.." && pwd -W)
G=${GAME_DIR:-G:/SteamLibrary/steamapps/common/SplitSecond}
rm -f "$G/SplitSecondPad_shot$1.jpg"
echo "shot $1" >> "$P/re/padcmd.txt"
for i in 1 2 3 4 5 6 7 8 9 10; do [ -f "$G/SplitSecondPad_shot$1.jpg" ] && break; sleep 0.5; done
sleep 0.5
mkdir -p "$P/shots" && mv "$G/SplitSecondPad_shot$1.jpg" "$P/shots/ig$1.jpg"
python -I -c "
from PIL import Image
im=Image.open('$P/shots/ig$1.jpg'); im.resize((im.size[0]//3,im.size[1]//3)).save('$P/shots/ig$1_s.png')"
cat "$P/re/padstatus.txt"
