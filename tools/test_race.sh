#!/bin/sh
# Rebuilds, installs the DLL with test settings, starts the game and drives the virtual pad into a quick race.
# usage: tools/test_race.sh "<extra [Controller] ini line>" ["<extra [Game] ini line>"]
# (GAME_DIR overrides the default Steam path)
P=$(cd "$(dirname "$0")/.." && pwd -W)
G=${GAME_DIR:-G:/SteamLibrary/steamapps/common/SplitSecond}
mkdir -p $P/re
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='SplitSecond.exe' or Name='WerFault.exe'\" | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force -ErrorAction SilentlyContinue }"
sleep 3
cmd //c "$(cygpath -w $P/build.bat)" 2>&1 | grep -E "error|built"
cp $P/build/dinput8.dll $G/dinput8.dll
sed -e 's/^Log=0/Log=1/' -e "s/^\[Controller\]/[Controller]\nTestFeed=1\n$1/" -e "s/^\[Game\]/[Game]\n$2/" $P/SplitSecondPad.ini > $G/SplitSecondPad.ini
rm -f $G/SplitSecondPad.log
(python -I $P/tools/padfeed.py $P/re/padcmd.txt $P/re/padstatus.txt > $P/re/padfeed.out 2>&1 &)
sleep 2
echo "connect 0 1" >> $P/re/padcmd.txt
powershell -NoProfile -Command "Start-Process -FilePath '$G/SplitSecond.exe' -WorkingDirectory '$G'"
sleep 45
printf "press START\nwait 3000\npress START\nwait 5000\npress RIGHT\nwait 800\npress A\nwait 2500\npress A\nwait 2500\npress A\nwait 3000\npress A\nwait 30000\npress A\nwait 6000\npress A\nwait 1000\ntrigger R 1\n" >> $P/re/padcmd.txt
sleep 60
echo "shot 1" >> $P/re/padcmd.txt
sleep 3
cat $P/re/padstatus.txt
