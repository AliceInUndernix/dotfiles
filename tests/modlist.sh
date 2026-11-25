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
                  ("Base", "2.0"), ("Other", "1.0"), ("NeedsVirt", "1.0"),
                  ("ProvA", "1.0"), ("ProvB", "1.0"), ("ProvC", "1.0")):
    z = zipfile.ZipFile("%s/%s-%s.zip" % (www, name, ver), "w")
    z.writestr("%s/%s.txt" % (name, name), "%s %s\n" % (name, ver))
    z.close()
PY

# addmod ID VERSION DEPENDS(json array or empty) [PROVIDES [KSPMAX]]
addmod() {
    id=$1; ver=$2; deps=$3; prov=${4:-}; kmax=${5:-}
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
    printf '%s\t%s\t%s-%s.ckan\t\t\t%s\tpackage\tstable\t%s\n' "$id" \
        "$ver" "$id" "$ver" "$kmax" "$prov" >> "$S/index"
    [ -z "$prov" ] || printf '%s\t%s\n' "$prov" "$id" >> "$S/provides"
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
addmod NeedsVirt 1.0 '[{"name":"Virt"}]'
addmod Other 1.0
addmod ProvA 1.0 '' Virt
addmod ProvB 1.0 '' Virt 1.0.5
addmod ProvC 1.0
addsum App 1.0; addsum Base 2.0; addsum Lib 1.0; addsum NeedsVirt 1.0
addsum Other 1.0; addsum ProvA 1.0; addsum ProvB 1.0; addsum ProvC 1.0

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

echo "== importing CKAN lists"
cat > "$W/pack.ckan" <<'EOT'
{"spec_version":"v1.18","identifier":"mypack","name":"My pack",
"kind":"metapackage","abstract":"x","version":"2026.11.25",
"depends":[{"name":"App","version":"1.0"},
{"any_of":[{"name":"Base","version":"1.0"},{"name":"Other"}]}],
"recommends":[{"name":"Other","min_version":"0.5"}],
"suggests":[{"name":"Lib"}]}
EOT
kk modlist import "$W/pack.ckan" >/dev/null 2>&1
t "a .ckan modpack becomes the modlist pack" test -f "$S/modlists/pack"
t "with depends, any_of's first and recommends" sh -c \
    "grep -q '^App 1.0$' '$S/modlists/pack' &&
    grep -q '^Base 1.0$' '$S/modlists/pack' &&
    grep -q '^Other$' '$S/modlists/pack'"
t "but not suggests" sh -c "! grep -q '^Lib' '$S/modlists/pack'"
t "and it is listed" sh -c \
    "\"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist | grep -q '^pack .*3 modules'"
cat > "$W/registry.json" <<'EOT'
{"registry_version":3,"installed_modules":{
"App":{"source_module":{"identifier":"App","version":"1.0"},
"installed_files":{"GameData/App/App.txt":{}},"auto_installed":false},
"Lib":{"source_module":{"identifier":"Lib","version":"1.0"},
"auto_installed":true}}}
EOT
kk modlist import "$W/registry.json" reg >/dev/null 2>&1
t "registry.json: the chosen mods, not the dependencies" sh -c \
    "grep -q '^App 1.0$' '$S/modlists/reg' && ! grep -q '^Lib' \
    '$S/modlists/reg'"
printf 'App mod (App 1.0)\r\nThe (Base) mod (Base 2.0)\r\n' \
    > "$W/list.txt"
kk modlist import "$W/list.txt" >/dev/null 2>&1
t "plain text \"Name (id version)\"" sh -c \
    "grep -q '^App 1.0$' '$S/modlists/list' &&
    grep -q '^Base 2.0$' '$S/modlists/list'"
printf -- '- **App mod** `App 1.0`\n- **Base mod** `Base 2.0`\n' \
    > "$W/md.md"
kk modlist import "$W/md.md" >/dev/null 2>&1
t "Markdown" sh -c "grep -q '^Base 2.0$' '$S/modlists/md'"
printf '[LIST]\n[*][B]App mod[/B] (App 1.0)\n[/LIST]\n' > "$W/bb.txt"
kk modlist import "$W/bb.txt" >/dev/null 2>&1
t "BBCode" sh -c "grep -q '^App 1.0$' '$S/modlists/bb' &&
    [ \$(grep -vc '^#' '$S/modlists/bb') = 1 ]"
printf 'identifier,version,name,abstract\nApp,1.0,"App, the mod",x\n' \
    > "$W/c.csv"
printf '"Other","1.0","Other mod",x\n' >> "$W/c.csv"
kk modlist import "$W/c.csv" >/dev/null 2>&1
t "CSV" sh -c "grep -q '^App 1.0$' '$S/modlists/c' &&
    grep -q '^Other 1.0$' '$S/modlists/c'"
printf 'identifier\tversion\tname\nBase\t2.0\tBase mod\n' > "$W/t.tsv"
kk modlist import "$W/t.tsv" >/dev/null 2>&1
t "TSV" sh -c "grep -q '^Base 2.0$' '$S/modlists/t'"
printf 'Installed Modules in /x:\n\n- App 1.0\n^ Base 1.0\n' > "$W/out"
printf 'Legend: -: Up to date. ^: Upgradable.\n' >> "$W/out"
kk modlist import "$W/out" cli >/dev/null 2>&1
t "ckan list output" sh -c "grep -q '^App 1.0$' '$S/modlists/cli' &&
    grep -q '^Base 1.0$' '$S/modlists/cli' &&
    [ \$(grep -vc '^#' '$S/modlists/cli') = 2 ]"
printf '{"depends":[{"name":"../../x"},{"name":"Ok-1"}]}' > "$W/evil.ckan"
kk modlist import "$W/evil.ckan" >/dev/null 2>&1
t "a bad identifier is passed over" sh -c \
    "! grep -q 'x' '$S/modlists/evil' && grep -q '^Ok-1$' '$S/modlists/evil'"
printf 'nothing here at all\n' > "$W/none.txt"
t "a file with no mods is refused" sh -c \
    "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist import '$W/none.txt' \
    2>/dev/null && test ! -e '$S/modlists/none'"
printf '{"depends":[' > "$W/broken.ckan"
t "broken JSON is refused" sh -c \
    "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 modlist import '$W/broken.ckan' \
    2>/dev/null"
kk remove App Base Other >/dev/null 2>&1
kk modlist pin pack >/dev/null 2>&1
t "the imported modpack installs" sh -c \
    "\"$KK\" -g '$G' -s '$S' -k 1.12.5 list | grep -q '^App 1.0$' &&
    \"$KK\" -g '$G' -s '$S' -k 1.12.5 list | grep -q '^Base 1.0$' &&
    \"$KK\" -g '$G' -s '$S' -k 1.12.5 list | grep -q '^Other 1.0$'"

echo "== a file given straight to install and show"
kk remove App Base Other >/dev/null 2>&1
rm -f "$S/modlists/pack"
(cd "$W" && "$KK" -g "$G" -s "$S" -k 1.12.5 -y modlist install pack.ckan \
    > "$W/out1" 2>&1)
t "modlist install FILE.ckan in the current directory imports it" \
    test -f "$S/modlists/pack"
t "and says so" grep -q \
    'imported 3 mods from the CKAN modpack into the modlist pack' "$W/out1"
t "and installs it" listed '^App 1.0$'
t "modlist show by path works" sh -c "\"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 modlist show '$W/list.txt' | grep -q 'App'"
printf 'Other\n' > "$W/plain"
kk modlist install "$W/plain" >/dev/null 2>&1
t "a plain list by path is used, not kept" sh -c "test ! -e \
    '$S/modlists/plain'"
t "and installed" listed '^Other 1.0$'
t "an unknown name says so" sh -c "\"$KK\" -g '$G' -s '$S' -k 1.12.5 \
    modlist install nosuch.ckan 2>&1 | grep -q 'no modlist or file'"
kk remove App Base Other >/dev/null 2>&1

echo "== a dependency several mods provide"
kk install NeedsVirt >/dev/null 2>&1
t "the one provider that fits the game is taken" listed '^ProvA 1.0 (dep'
kk remove NeedsVirt >/dev/null 2>&1
t "and goes with what needed it" sh -c "! \"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 list | grep -q '^ProvA'"
printf '%s\t%s\n' Virt ProvC >> "$S/provides"
sed 's/^\(ProvC\t.*\t\)$/\1Virt/' "$S/index" > "$S/index.new"
mv "$S/index.new" "$S/index"
t "two that fit: refused, naming both" sh -c "\"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 -y install NeedsVirt 2>&1 | grep -q 'ProvA, ProvC'"
python3 -I - "$KK" "$G" "$S" > "$W/asked" 2>&1 <<'PY'
import os, pty, select, sys
kk, g, s = sys.argv[1:4]
pid, fd = pty.fork()
if pid == 0:
    os.execv(kk, [kk, "-g", g, "-s", s, "-k", "1.12.5", "install",
                  "NeedsVirt"])
def rd():
    out = b""
    while True:
        r, _, _ = select.select([fd], [], [], 2)
        if not r:
            break
        try:
            d = os.read(fd, 65536)
        except OSError:
            break
        if not d:
            break
        out += d
    return out
o = rd()
os.write(fd, b"2\n")
o += rd()
os.write(fd, b"y\n")
o += rd()
os.waitpid(pid, 0)
sys.stdout.write(o.decode("ascii", "replace"))
PY
t "on a terminal it asks which" grep -q 'which one? \[1-2' "$W/asked"
t "and installs the one picked" listed '^ProvC 1.0 (dependency)'
kk remove NeedsVirt >/dev/null 2>&1
kk install NeedsVirt ProvC >/dev/null 2>&1
t "the one named with it wins" sh -c "\"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 list | grep -q '^ProvC 1.0' && ! \"$KK\" -g '$G' -s '$S' \
    -k 1.12.5 list | grep -q '^ProvA'"
kk remove NeedsVirt ProvC >/dev/null 2>&1

echo "== unneeded dependencies"
kk remove App >/dev/null 2>&1
t "removing App took Lib too" sh -c "! \"$KK\" -g '$G' -s '$S' -k 1.12.5 \
    list | grep -q '^Lib'"

kill $SRV 2>/dev/null
echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
