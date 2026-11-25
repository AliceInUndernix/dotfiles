#!/bin/sh
# Hostile archives against kspkan.  Needs python3 only to build the zips
# and to serve them on 127.0.0.1.  Touches nothing outside a scratch dir.
KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
W=${TMPDIR:-/tmp}/kspkan-evil.$$
G=$W/game
S=$W/state
PORT=${PORT:-8099}
PASS=0
FAIL=0
mkdir -p "$G/GameData" "$S/meta" "$W/www" "$W/outside"

python3 -I - "$W/www" <<'PY'
import sys, zipfile, struct
www = sys.argv[1]
z = zipfile.ZipFile(www + "/evil.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Evil/ok.txt", "fine")
z.writestr("Evil/../../escape.txt", "escaped")
z.writestr("Evil/sub/../../escape2.txt", "escaped")
z.writestr("Evil/..\\escape3.txt", "escaped")
z.writestr("Evil//double.txt", "x")
z.close()
z = zipfile.ZipFile(www + "/win.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Win\\Plugins\\a.cfg", "made on Windows")
z.writestr("Win\\..\\..\\escape4.txt", "escaped")
z.close()
z = zipfile.ZipFile(www + "/link.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Evil/ok.txt", "fine")
z.close()
z = zipfile.ZipFile(www + "/bomb.zip", "w", zipfile.ZIP_DEFLATED)
z.writestr("Bomb/zeros.bin", b"\0" * (300 * 1024 * 1024))
z.close()
d = bytearray(open(www + "/bomb.zip", "rb").read())
i = d.rfind(b"PK\x01\x02")
struct.pack_into("<I", d, i + 24, 1000)          # lie about the size
open(www + "/bomb.zip", "wb").write(d)
PY

addmod() {   # addmod ID ZIPFILE
    id=$1; zip=$2
    sum=$(sha256sum "$W/www/$zip" | cut -d' ' -f1)
    mkdir -p "$S/meta/$id"
    cat > "$S/meta/$id/$id-1.0.ckan" <<EOT
{"spec_version":"v1.4","identifier":"$id","name":"$id","abstract":"test",
"version":"1.0","download":"http://127.0.0.1:$PORT/$zip",
"download_hash":{"sha256":"$sum"},
"install":[{"find":"$id","install_to":"GameData"}]}
EOT
    printf '%s\t1.0\t%s-1.0.ckan\t\t\t\tpackage\tstable\t\n' "$id" "$id" \
        >> "$S/index"
    printf '%s\t1.0\t%s\ttest\t\t\t\t\t0\t\n' "$id" "$id" >> "$S/summary"
}
: > "$S/index"; : > "$S/summary"; : > "$S/provides"; : > "$S/counts"
# the index is kept sorted by identifier
addmod Bomb bomb.zip
addmod Evil evil.zip
addmod Link link.zip
addmod Win win.zip

(cd "$W/www" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1 \
    >/dev/null 2>&1) &
SRV=$!
sleep 1
kk() { "$KK" -g "$G" -s "$S" -k 1.12.5 -y "$@"; }
ok() { PASS=$((PASS + 1)); echo "ok    $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL  $1"; }

echo "== path traversal in the archive"
kk install Evil >/dev/null 2>&1
[ -f "$G/GameData/Evil/ok.txt" ] && ok "the safe file is installed" ||
    bad "the safe file is installed"
n=$(find "$W" -name 'escape*' -o -name 'double.txt' | wc -l | tr -d ' ')
[ "$n" = 0 ] && ok "no unsafe file written anywhere" ||
    bad "unsafe files were written: $n"
[ ! -e "$G/escape.txt" ] && ok "nothing next to GameData" ||
    bad "nothing next to GameData"
kk remove Evil >/dev/null 2>&1

echo "== a zip made on Windows, with \\ between names"
kk install Win >/dev/null 2>&1
[ -f "$G/GameData/Win/Plugins/a.cfg" ] && ok "installed with / paths" ||
    bad "installed with / paths"
n=$(find "$W" -name 'escape4*' | wc -l | tr -d ' ')
[ "$n" = 0 ] && ok "no \\..\\ escape" || bad "\\..\\ escaped: $n"
kk remove Win >/dev/null 2>&1

echo "== a symlink where a folder should be"
ln -s "$W/outside" "$G/GameData/Link"
kk install Link >/dev/null 2>&1
rc=$?
[ -z "$(ls "$W/outside")" ] && ok "nothing written through the symlink" ||
    bad "wrote through the symlink: $(ls "$W/outside")"
[ $rc -ne 0 ] && ok "install is refused" || bad "install should be refused"
rm -f "$G/GameData/Link"

echo "== a zip that lies about its size (bomb)"
kk install Bomb >/dev/null 2>&1
rc=$?
[ $rc -ne 0 ] && ok "install fails cleanly (exit $rc)" ||
    bad "install should fail"
[ ! -e "$G/GameData/Bomb" ] && ok "no half written files" ||
    bad "half written files"
[ -z "$("$KK" -g "$G" -s "$S" -k 1.12.5 list)" ] && ok "nothing registered" ||
    bad "something registered"

kill $SRV 2>/dev/null
echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
