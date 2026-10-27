#!/bin/sh
# usage: dec.sh <script> <out> args...   (runs a ghidra_scripts postScript on the analyzed dump, read-only)
# GHIDRA_HOME points at the Ghidra install; the project lives in ghidra/ (see README, not in the repo)
P=$(cd "$(dirname "$0")/.." && pwd)
GHIDRA_HOME=${GHIDRA_HOME:-D:/ghidra_12.1.4_PUBLIC_20260921/ghidra_12.1.4_PUBLIC}
S=$1; shift
"$GHIDRA_HOME/support/analyzeHeadless.bat" "$P/ghidra" SplitSecond -process SplitSecond_dump.exe -noanalysis -readOnly -scriptPath "$P/ghidra_scripts" -postScript $S "$@" > "$P/ghidra/last_run.log" 2>&1
grep -i "error\|exception" "$P/ghidra/last_run.log" | head -5
