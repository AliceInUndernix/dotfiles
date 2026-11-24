#!/bin/sh
# Instances: remembered game folders.  Uses a private data folder.
KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
W=${TMPDIR:-/tmp}/kspkan-inst.$$
PASS=0
FAIL=0
mkdir -p "$W/data" "$W/a/GameData" "$W/b/GameData" "$W/empty"
export XDG_DATA_HOME=$W/data
export HOME=$W/home
mkdir -p "$HOME"
ok() { PASS=$((PASS + 1)); echo "ok    $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL  $1"; }
t() { desc=$1; shift; if "$@" >/dev/null 2>&1; then ok "$desc"; else bad "$desc"; fi; }
f() { desc=$1; shift; if "$@" >/dev/null 2>&1; then bad "$desc"; else ok "$desc"; fi; }
K() { "$KK" "$@"; }

t "add the first instance" K add one "$W/a"
t "add a second one" K add two "$W/b"
f "a folder without GameData is refused" K add three "$W/empty"
f "a bad name is refused" K add "bad name" "$W/a"
out=$(K instances)
echo "$out" | grep -q '^\* one ' && ok "the first one is the default" ||
    bad "the first one is the default"
t "use the second" K use two
K instances | grep -q '^\* two ' && ok "default changed and remembered" ||
    bad "default changed and remembered"
(cd "$W/a" && K add rel . >/dev/null 2>&1)
K instances | grep -q "rel .*$W/a\$" && ok "a relative folder is stored in full" ||
    bad "a relative folder is stored in full"
f "an unknown instance is refused" K -i nosuch list
t "list works for a named instance" K -i one list
t "forget removes it" K forget rel
K instances | grep -q ' rel ' && bad "forgotten instance is gone" ||
    ok "forgotten instance is gone"
[ -d "$W/a/GameData" ] && ok "the game folder was not touched" ||
    bad "the game folder was not touched"
K forget two >/dev/null
K instances | grep -q '^\* one ' && ok "the default moves on when it is forgotten" ||
    bad "the default moves on when it is forgotten"
f "no instances and no game folder: asks, and fails without a terminal" \
    sh -c "cd '$W/empty' && XDG_DATA_HOME='$W/none' '$KK' </dev/null list"

echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
