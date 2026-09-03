#!/bin/zsh
# Usage: pdbench.sh <pd-src-dir> <seconds> <dsp 0|1> [label]
REPO=${REPO:-$(git rev-parse --show-toplevel)}
SRC="$1"; SECS="${2:-15}"; DSP="${3:-1}"; LABEL="${4:-run}"
PD=/Applications/Pd-0.56-2.app/Contents/Resources/bin/pd
EXT="$REPO"/formuls-0.3.0-beta.app/Contents/Resources/pd/externals
W=$(mktemp -d)
cp -R "$SRC"/. "$W"/ ; mkdir -p "$W/externals"; cp "$EXT"/*.pd_darwin "$W/externals/" 2>/dev/null
MS=$(( SECS * 1000 ))
cat > "$W/_bench.pd" <<PQ
#N canvas 0 0 300 200 10;
#X obj 20 20 loadbang;
#X obj 20 45 del 3000;
#X msg 20 70 \; pd dsp $DSP;
#X obj 20 95 del $MS;
#X msg 20 120 \; pd quit;
#X connect 0 0 1 0;
#X connect 1 0 2 0;
#X connect 1 0 3 0;
#X connect 3 0 4 0;
PQ
cd "$W"
/usr/bin/time "$PD" -nogui -noaudio -nomidi -open _main.pd -open _bench.pd > /dev/null 2> "$W/_t.txt"
awk -v l="$LABEL" -v d="$DSP" '/real/{printf "%-26s dsp=%s  user=%6.2fs  sys=%5.2fs  wall=%6.2fs\n", l, d, $3, $5, $1}' "$W/_t.txt"
rm -rf "$W"
