#!/bin/sh
# An upgrade writes over the old files and takes away only the rest;
# a removed .cfg is copied to state/removed first.  Needs python3 to
# build the zips and serve them on 127.0.0.1; touches nothing else.
KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
W=${TMPDIR:-/tmp}/kspkan-upgrade.$$
G=$W/game
S=$W/state
PORT=${PORT:-8098}
PASS=0
FAIL=0
mkdir -p "$G/GameData" "$S/meta" "$W/www"

python3 -I - "$W/www" <<'PY'
import sys, zipfile
www = sys.argv[1]
z = zipfile.ZipFile(www + "/up1.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Up/a.cfg", "one")
z.writestr("Up/old.txt", "old")
z.writestr("Up/keep.txt", "keep")
z.close()
z = zipfile.ZipFile(www + "/up2.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Up/a.cfg", "two")
z.writestr("Up/keep.txt", "keep2")
z.writestr("Up/new.txt", "new")
z.close()
PY

: > "$S/index"; : > "$S/summary"; : > "$S/provides"; : > "$S/counts"
mkdir -p "$S/meta/Up"
for v in 2 1; do
    zip=up$v.zip
    sum=$(sha256sum "$W/www/$zip" | cut -d' ' -f1)
    cat > "$S/meta/Up/Up-$v.0.ckan" <<EOT
{"spec_version":"v1.4","identifier":"Up","name":"Up","abstract":"test",
"version":"$v.0","download":"http://127.0.0.1:$PORT/$zip",
"download_hash":{"sha256":"$sum"},
"install":[{"find":"Up","install_to":"GameData"}]}
EOT
    printf 'Up\t%s.0\tUp-%s.0.ckan\t\t\t\tpackage\tstable\t\n' "$v" "$v" \
        >> "$S/index"
done
printf 'Up\t2.0\tUp\ttest\t\t\t\t\t0\t\n' >> "$S/summary"

(cd "$W/www" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1 \
    >/dev/null 2>&1) &
SRV=$!
sleep 1
kk() { "$KK" -g "$G" -s "$S" -k 1.12.5 -y "$@"; }
ok() { PASS=$((PASS + 1)); echo "ok    $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL  $1"; }

kk install Up=1.0 >/dev/null 2>&1
[ -f "$G/GameData/Up/old.txt" ] && ok "1.0 is installed" || bad "1.0 is installed"
kk upgrade >/dev/null 2>&1
[ "$(cat "$G/GameData/Up/a.cfg" 2>/dev/null)" = two ] &&
    ok "a.cfg is the new one" || bad "a.cfg is the new one"
[ -f "$G/GameData/Up/new.txt" ] && ok "new file is there" || bad "new file is there"
[ ! -e "$G/GameData/Up/old.txt" ] && ok "the file only 1.0 had is gone" ||
    bad "the file only 1.0 had is gone"
kk list 2>/dev/null | grep -q '^Up 2.0' && ok "2.0 is listed" || bad "2.0 is listed"
echo mine > "$G/GameData/Up/a.cfg"
kk remove Up >/dev/null 2>&1
[ ! -d "$G/GameData/Up" ] && ok "removed" || bad "removed"
[ "$(cat "$S/removed/Up/GameData/Up/a.cfg" 2>/dev/null)" = mine ] &&
    ok "my edited .cfg was copied first" || bad "my edited .cfg was copied first"

kill "$SRV" 2>/dev/null
rm -rf "$W"
echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
