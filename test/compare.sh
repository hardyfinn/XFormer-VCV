#!/bin/bash
# Compares the C++ engines with the JavaScript port (which matches the firmware
# simulator) on saved scenarios: every trace must be byte-identical.
#   test/compare.sh <engine> <scenario dir> <path to m4l/test/trace.js>
set -u
eng=$1; dir=$2; js=$3
here=$(dirname "$0")
g++ -std=c++17 -O2 -o /tmp/xf_trace "$here/trace.cpp" || exit 1
pass=0; fail=0
for f in "$dir"/s*.txt; do
  /tmp/xf_trace "$eng" "$f" /tmp/xf_c.txt
  node "$js" "$eng" "$f" /tmp/xf_j.txt
  if cmp -s /tmp/xf_c.txt /tmp/xf_j.txt; then pass=$((pass+1)); else fail=$((fail+1)); [ $fail -le 5 ] && echo "FAIL $f"; fi
done
echo "$eng: pass $pass fail $fail"
