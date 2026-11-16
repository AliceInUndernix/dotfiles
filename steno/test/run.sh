#!/bin/sh
# Type test/strokes with test/dict.json and compare with test/expected,
# which is what Plover 5.4.2 gives for the same strokes.
cd "$(dirname "$0")/.." || exit 1
./steno -c test/config -t < test/strokes > test/out || exit 1
if cmp -s test/out test/expected; then
    rm -f test/out
    echo "test: ok"
else
    diff test/expected test/out
    exit 1
fi
