#!/bin/bash
# Runs the reference script on every D1 sample and compares against reference_shots.
BIN=${BIN:-/home/claude/dante/build-d1/dante98cr-headless}
OUT=${OUT:-/tmp/claude-0/d1ref}
REF=/home/claude/work/spec/dante98_cleanroom_spec/reference_shots
SCRIPT="60 shot:01_title 30 z 30 z 150 shot:02_start z 70 z 70 shot:03_after_ok x 60 shot:04_menu x 60 r*40 l*40 shot:05_walk"
$BIN --games /home/claude/samples -o /tmp/claude-0 2>/dev/null | grep -P '^Dante98\t' | cut -f2 | while read root; do
  rel=${root#/home/claude/samples/}
  name=$(echo "$rel" | cut -d/ -f2)
  mkdir -p $OUT/$name
  timeout 120 $BIN "$root" -k "$SCRIPT" -n 1500 -o $OUT/$name > /dev/null 2> $OUT/$name/log.txt || echo "$name: exit $?"
  echo "== $name"
  python3 /home/claude/dante/tools/cmp_ref.py $OUT/$name $REF/$name 2>/dev/null
done
