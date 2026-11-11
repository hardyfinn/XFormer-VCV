#!/bin/bash
# Checks the C++ engines against saved traces (made by the JavaScript port,
# which matches the firmware simulator). Every trace must be byte-identical.
#   test/golden.sh
set -u
here=$(cd "$(dirname "$0")" && pwd)
c++ -std=c++11 -O2 -o "$here/trace" "$here/trace.cpp" || exit 1
pass=0; fail=0
for dir in "$here"/golden/*/; do
  eng=$(basename "$dir")
  for f in "$dir"s*.txt; do
    "$here/trace" "$eng" "$f" /tmp/xf_golden.out
    if cmp -s /tmp/xf_golden.out "${f%.txt}.out"; then pass=$((pass+1)); else fail=$((fail+1)); echo "FAIL $eng $(basename "$f")"; fi
  done
done
echo "golden traces: $pass identical, $fail different"
[ $fail -eq 0 ]
