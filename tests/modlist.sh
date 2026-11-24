#!/bin/sh
# Modlists, "chosen" and "dependency", and what needs what, on a small
# made-up module list.  Needs python3 only to build the zips and to
# serve them on 127.0.0.1.  Touches nothing outside a scratch dir.
KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
W=${TMPDIR:-/tmp}/kspkan-modlist.$$
G=$W/game
S=$W/state
PORT=${PORT:-8098}
PASS=0
FAIL=0
mkdir -p "$G/GameData" "$S/meta" "$W/www"

python3 -I - "$W/www" <<'PY'
import sys, zipfile
www = sys.argv[1]
for name, ver in (("Lib", "1.0"), ("App", "1.0"), ("Base", "1.0"),
                  ("Base", "2.0"), ("Other", "1.0")):
    z = zipfile.ZipFile("%s/%s-%s.zip" % (www, name, ver), "w")
    z.writestr("%s/%s.txt" % (name, name), "%s %s\n" % (name, ver))
    z.close()
PY

# addmod ID VERSION DEPENDS(json array or empty)
addmod() {
    id=$1; ver=$2; deps=$3
    zip=$id-$ver.zip
    dep=
    [ -z "$deps" ] || dep="\"depends\":$deps,"
    sum=$(sha256sum "$W/www/$zip" | cut -d' ' -f1)
    mkdir -p "$S/meta/$id"
    cat > "$S/meta/$id/$id-$ver.ckan" <<EOT
{"spec_version":"v1.4","identifier":"$id","name":"$id mod",
"abstract":"the $id test module","version":"$ver",
"description":"A longer text about $id.\n\nIt has two paragraphs.",
"download":"http://127.0.0.1:$PORT/$zip",
"download_hash":{"sha256":"$sum"},$dep
"install":[{"find":"$id","install_to":"GameData"}]}
EOT
    printf '%s\t%s\t%s-%s.ckan\t\t\t\tpackage\tstable\t\n' "$id" "$ver" \
        "$id" "$ver" >> "$S/index"
}
addsum() {
    printf '%s\t%s\t%s mod\tthe %s test module\t\t\t\t\t0\t\n' \
        "$1" "$2" "$1" "$1" >> "$S/summary"
}
: > "$S/index"; : > "$S/summary"; : > "$S/provides"; : > "$S/counts"
# sorted by id, the newest version first
addmod App 1.0 '[{"name":"Lib"}]'
addmod Base 2.0
addmod Base 1.0
addmod Lib 1.0
addmod Other 1.0
addsum App 1.0; addsum Base 2.0; addsum Lib 1.0; addsum Other 1.0

(cd "$W/www" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1 \
    >/dev/null 2>&1) &
SRV=$!
sleep 1
kk() { "$KK" -g "$G" -s "$S" -k 1.12.5 -y "$@"; }
ok() { PASS=$((PASS + 1)); echo "ok    $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL  $1"; }
t() { desc=$1; shift; if "$@"; then ok "$desc"; else bad "$desc"; fi; }
listed() { kk list 2>/dev/null | grep -q "$1"; }
found() { kk search "$1" 2>/dev/null | grep -q "$2"; }

echo "== chosen and dependency"
kk install App Base=1.0 >/dev/null 2>&1
t "App is chosen" listed '^App 1.0$'
t "Lib is a dependency needed by App" \
    listed '^Lib 1.0 (dependency) needed by App$'
t "is:dep finds Lib" found is:dep '^i Lib'
t "is:chosen finds App, not Lib" sh -c \
    "\"$KK\" -g '$G' -s '$S' -k 1.12.5 search is:chosen 2>/dev/null |
    grep -q '^i App' && ! \"$KK\" -g '$G' -s '$S' -k 1.12.5 search \
    is:chosen 2>/dev/null | grep -q Lib"
t "is:needed finds Lib" found is:needed '^i Lib'
t "show tells why Lib is there" sh -c \
    "\"$KK\" -g '$G' -s '$S' -k 1.12.5 show Lib | grep -q 'needed by: App'"

echo "== keep and auto"
kk keep Lib >/dev/null 2>&1
t "keep makes Lib chosen" listed '^Lib 1.0 needed by App$'
kk auto Lib >/dev/null 2>&1
t "auto makes it a dependency again" listed '^Lib 1.0 (dependency)'
kk install Lib >/dev/null 2>&1
t "installing an installed dependency makes it chosen" \
    listed '^Lib 1.0 needed by App$'
kk auto Lib >/dev/null 2>&1

echo "== modlists"
kk modlist save one >/dev/null 2>&1
t "saved in modlists/" test -f "$S/modlists/one"
t "it has App and Base, not the dependency" sh -c \
    "grep -q '^App 1.0$' '$S/modlists/one' &&
    grep -q '^Base 1.0$' '$S/modlists/one' &&
    ! grep -q '^Lib' '$S/modlists/one'"
t "modlist list names it" sh -c \
    "\"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist | grep -q '^one .*2 modules'"
kk remove App Base >/dev/null 2>&1
t "all removed" sh -c "[ -z \"\$(\"$KK\" -g '$G' -s '$S' -k 1.12.5 list)\" ]"
kk modlist install one >/dev/null 2>&1
t "modlist install brings App back" listed '^App 1.0$'
t "with its dependency" listed '^Lib 1.0 (dependency)'
t "and Base at the newest" listed '^Base 2.0$'
kk modlist pin one >/dev/null 2>&1
t "modlist pin puts Base 1.0 back" listed '^Base 1.0$'
kk install Other >/dev/null 2>&1
kk modlist sync one >/dev/null 2>&1
t "modlist sync removes Other" sh -c "! kk_list=\$(\"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 list) || ! echo \"\$kk_list\" | grep -q '^Other'"
t "and keeps App" listed '^App 1.0$'
printf 'Other\n# a comment\nNoSuchMod 1.0\n' > "$W/hand.txt"
kk modlist install "$W/hand.txt" >/dev/null 2>&1
t "a hand-written list by path installs Other" listed '^Other 1.0$'
kk modlist delete one >/dev/null 2>&1
t "delete removes it" test ! -e "$S/modlists/one"
t "and from the names" sh -c \
    "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist | grep -q '^one '"
t "a bad name is refused" sh -c \
    "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist save '.x' 2>/dev/null"

echo "== unneeded dependencies"
kk remove App >/dev/null 2>&1
t "removing App took Lib too" sh -c "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 \
    list | grep -q '^Lib'"

kill $SRV 2>/dev/null
echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
