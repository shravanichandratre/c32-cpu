#!/usr/bin/env bash
# run_examples.sh - run every example program and compare its output with
# the recorded expected output.
#
#   examples/NAME.asm         stdout must equal examples/expected/NAME.out
#                             (stdin comes from examples/inputs/NAME.txt if present)
#   examples/faults/NAME.asm  stdout+stderr must equal examples/faults/expected/NAME.out
#                             and the simulator must exit with status 3 (CPU fault)
#
# Usage: scripts/run_examples.sh [path/to/c32sim]
#        scripts/run_examples.sh --update   (re-record expected outputs)
set -u
cd "$(dirname "$0")/.."

SIM=./build/c32sim
UPDATE=0
for arg in "$@"; do
    case "$arg" in
        --update) UPDATE=1 ;;
        *) SIM="$arg" ;;
    esac
done

if [ ! -x "$SIM" ]; then
    echo "error: simulator '$SIM' not found - run 'make' first" >&2
    exit 1
fi

pass=0
fail=0
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT

check() {  # name expected_file expected_status actual_status
    local name=$1 expected=$2 want_status=$3 status=$4
    if [ "$UPDATE" = 1 ]; then
        mkdir -p "$(dirname "$expected")"
        cp "$tmp" "$expected"
        echo "  recorded  $name"
        return
    fi
    if [ ! -f "$expected" ]; then
        echo "  FAIL  $name  (missing $expected)"
        fail=$((fail + 1))
    elif ! diff -u "$expected" "$tmp" > /dev/null; then
        echo "  FAIL  $name  (output differs)"
        diff -u "$expected" "$tmp" | head -20
        fail=$((fail + 1))
    elif [ "$status" != "$want_status" ]; then
        echo "  FAIL  $name  (exit status $status, expected $want_status)"
        fail=$((fail + 1))
    else
        echo "  PASS  $name"
        pass=$((pass + 1))
    fi
}

echo "== example programs"
for src in examples/*.asm; do
    name=$(basename "$src" .asm)
    input=examples/inputs/$name.txt
    [ -f "$input" ] || input=/dev/null
    "$SIM" "$src" < "$input" > "$tmp" 2>/dev/null
    check "$name" "examples/expected/$name.out" 0 $?
done

echo "== fault demonstrations (expected to trap)"
for src in examples/faults/*.asm; do
    name=$(basename "$src" .asm)
    "$SIM" "$src" < /dev/null > "$tmp" 2>&1
    check "faults/$name" "examples/faults/expected/$name.out" 3 $?
done

if [ "$UPDATE" = 1 ]; then
    exit 0
fi
echo
echo "examples: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
