#!/bin/bash
# Runs the reference script on every sample game (D1 and D2) and compares against reference_shots.
# Prints one line per game: name engine matched/total, plus any differing shots.
# Environment:
#   OPENDT_SAMPLES  root of the sample games (default: ./samples)
#   OPENDT_REF      reference_shots directory from the clean-room spec (required)
#   BIN             headless binary (default: build/opendt-headless in the repository)
#   OUT             output directory (default: /tmp/opendt_refs)
HERE=$(cd "$(dirname "$0")" && pwd)
BIN=${BIN:-$HERE/../build/opendt-headless}
OUT=${OUT:-/tmp/opendt_refs}
SAMPLES=${OPENDT_SAMPLES:-samples}
SAMPLES=${SAMPLES%/}
REF=${OPENDT_REF:?set OPENDT_REF to the reference_shots directory}
SCRIPT="60 shot:01_title 30 z 30 z 150 shot:02_start z 70 z 70 shot:03_after_ok x 60 shot:04_menu x 60 r*40 l*40 shot:05_walk"
mkdir -p "$OUT"
$BIN --games "$SAMPLES" -o "$OUT" 2>/dev/null | while IFS=$'\t' read kind root; do
  rel=${root#$SAMPLES/}
  name=$(echo "$rel" | cut -d/ -f2)
  mkdir -p "$OUT/$name"
  timeout 120 $BIN "$root" -k "$SCRIPT" -n 1500 -o "$OUT/$name" > /dev/null 2> "$OUT/$name/log.txt"
  rc=$?
  if [ -d "$REF/$name" ]; then
    python3 - "$OUT/$name" "$REF/$name" "$name" "$kind" "$rc" <<'EOF'
import sys, os
from PIL import Image, ImageChops
out, ref, name, kind, rc = sys.argv[1:6]
ok = 0; bad = []
names = sorted(os.listdir(ref))
for n in names:
    a = os.path.join(out, n)
    if not os.path.exists(a):
        bad.append(n + ':missing'); continue
    d = ImageChops.difference(Image.open(a).convert('RGB'), Image.open(os.path.join(ref, n)).convert('RGB'))
    if d.getbbox() is None: ok += 1
    else: bad.append(n + ':' + str(d.getbbox()))
print('%-10s %-10s %d/%d rc=%s %s' % (name, kind, ok, len(names), rc, ' '.join(bad)))
EOF
  else
    echo "$(printf '%-10s %-10s' $name "$kind") (no reference) rc=$rc"
  fi
done
