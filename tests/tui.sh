#!/bin/sh
# The full screen, driven as a user would: keys and mouse clicks sent
# through a pseudo terminal.  Marks that survive a failed install and a
# restart, import with Tab completion (add on top, replace), export,
# games found on their own, mistakes.  Needs python3 only to build the
# zips, to serve them on 127.0.0.1 and to be the terminal.  Touches
# nothing outside a scratch dir (HOME is pointed there too).
KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
W=${TMPDIR:-/tmp}/kspkan-tui.$$
G=$W/game
S=$W/state
PORT=${PORT:-8097}
PASS=0
FAIL=0
mkdir -p "$G/GameData/Squad" "$S/meta" "$W/www" "$W/home"

python3 -I - "$W/www" <<'PY'
import sys, zipfile
www = sys.argv[1]
for name in ("App", "Lib", "Base", "Other", "Broken"):
    z = zipfile.ZipFile("%s/%s-1.0.zip" % (www, name), "w")
    z.writestr("%s/%s.txt" % (name, name), name + "\n")
    z.close()
PY

addmod() {   # addmod ID DEPENDS
    id=$1; deps=$2
    dep=
    [ -z "$deps" ] || dep="\"depends\":$deps,"
    sum=$(sha256sum "$W/www/$id-1.0.zip" | cut -d' ' -f1)
    mkdir -p "$S/meta/$id"
    cat > "$S/meta/$id/$id-1.0.ckan" <<EOT
{"spec_version":"v1.4","identifier":"$id","name":"$id mod",
"abstract":"the $id test module","version":"1.0",
"download":"http://127.0.0.1:$PORT/$id-1.0.zip",
"download_hash":{"sha256":"$sum"},$dep
"install":[{"find":"$id","install_to":"GameData"}]}
EOT
    printf '%s\t1.0\t%s-1.0.ckan\t\t\t\tpackage\tstable\t\n' "$id" "$id" \
        >> "$S/index"
    printf '%s\t1.0\t%s mod\tthe %s test module\t\t\t\t\t0\t\n' \
        "$id" "$id" "$id" >> "$S/summary"
}
: > "$S/index"; : > "$S/summary"; : > "$S/provides"; : > "$S/counts"
addmod App '[{"name":"Lib"}]'
addmod Base
addmod Broken '[{"name":"NoSuchThing"}]'
addmod Lib
addmod Other

# the terminal: drive.py KEYS... where a key is text, \r, \t, \e,
# "wait", or "click X Y", "rclick X Y", "wheel up|down X Y"
cat > "$W/drive.py" <<'PY'
import os, pty, select, sys, time, re
args = sys.argv[1:]
i = args.index("--")
cmd, keys = args[:i], args[i + 1:]
pid, fd = pty.fork()
if pid == 0:
    os.environ["TERM"] = "xterm"
    os.execv(cmd[0], cmd)
import fcntl, termios, struct
fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 120, 0, 0))
out = b""
def rd(t=0.6):
    global out
    while True:
        r, _, _ = select.select([fd], [], [], t)
        if not r:
            return
        try:
            d = os.read(fd, 65536)
        except OSError:
            return
        if not d:
            return
        out += d
rd(1.0)
for k in keys:
    p = k.split()
    if k == "wait":
        rd(2.0)
        continue
    if p and p[0] == "dclick":
        for n in (0, 1):
            os.write(fd, ("\033[<0;%s;%sM\033[<0;%s;%sm" %
                          (p[1], p[2], p[1], p[2])).encode())
        rd()
        continue
    if p and p[0] in ("click", "rclick", "wheel"):
        if p[0] == "wheel":
            b, x, y = (64 if p[1] == "up" else 65), p[2], p[3]
        else:
            b, x, y = (0 if p[0] == "click" else 2), p[1], p[2]
        os.write(fd, ("\033[<%d;%s;%sM" % (b, x, y)).encode())
        os.write(fd, ("\033[<%d;%s;%sm" % (b, x, y)).encode())
    else:
        os.write(fd, k.encode().decode("unicode_escape").encode())
    rd()
rd(1.0)
try:
    os.kill(pid, 9)
except OSError:
    pass
os.waitpid(pid, 0)
s = out.decode("ascii", "replace")
s = re.sub(r"\x1b\[[0-9;?<]*[a-zA-Z]", " ", s)
sys.stdout.write(s)
PY

(cd "$W/www" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1 \
    >/dev/null 2>&1) &
SRV=$!
sleep 1
export HOME="$W/home"
export XDG_DATA_HOME="$W/home/.local/share"
kk() { "$KK" -g "$G" -s "$S" -k 1.12.5 -y "$@"; }
tui() { python3 -I "$W/drive.py" "$KK" -g "$G" -s "$S" -k 1.12.5 -y -- \
    "$@"; }
ok() { PASS=$((PASS + 1)); echo "ok    $1"; }
bad() { FAIL=$((FAIL + 1)); echo "FAIL  $1"; }
t() { desc=$1; shift; if "$@"; then ok "$desc"; else bad "$desc"; fi; }
listed() { kk list 2>/dev/null | grep -q "$1"; }
unlisted() { ! listed "$1"; }
marked() { grep -qx "$1" "$S/marks" 2>/dev/null; }

echo "== marks are kept when something cannot go in"
tui '/App' '\r' ' ' '\x1b' '/Other' '\r' ' ' '\x1b' '/Broken' '\r' ' ' \
    '\x1b' '\r' wait '\r' 'q' > "$W/o1"
t "App went in" listed '^App 1.0$'
t "Other went in" listed '^Other 1.0$'
t "Broken did not" unlisted Broken
t "Broken is still marked" marked Broken
t "App and Other are no longer marked" sh -c \
    "! grep -qx App '$S/marks' && ! grep -qx Other '$S/marks'"
t "the screen says why" grep -q "NoSuchThing" "$W/o1"
t "and that the mark stays" grep -q "still marked" "$W/o1"

echo "== marks survive quitting"
tui 'q' > "$W/o2"
t "a restart tells about the marks" grep -q "still marked from last" \
    "$W/o2"
tui 'C' 'q' > /dev/null
t "C clears them, also on disk" test ! -e "$S/marks"
tui '\r' 'q' > "$W/o3"
t "Enter with nothing marked says so" grep -q "nothing marked" "$W/o3"

echo "== import with Tab completion"
cat > "$W/spack.ckan" <<'EOT'
{"spec_version":"v1.18","identifier":"spack","name":"S pack",
"kind":"metapackage","abstract":"x","version":"1",
"depends":[{"name":"App"},{"name":"Base"}]}
EOT
tui 'o' "$W/sp" '\t' '\r' 'a' 'q' > "$W/o4"
t "Tab completed the path and the pack opened" grep -q \
    "CKAN modpack, 2 mods" "$W/o4"
t "kept as the modlist spack" test -f "$S/modlists/spack"
t "add on top marks Base" marked Base
t "and nothing to remove" sh -c "! grep -qx Other '$S/marks'"
tui 'C' 'o' 'spa' '\t' '\r' 'r' 'q' > "$W/o5"
t "a modlist name completes too" grep -q "spack" "$W/o5"
t "replace marks Base to install" marked Base
t "and Other, which the pack lacks, to remove" marked Other
t "but not App, which it has" sh -c "! grep -qx App '$S/marks'"
tui '\r' wait '\r' 'q' > /dev/null
t "after Enter the game has Base" listed '^Base 1.0$'
t "and not Other" unlisted '^Other'
t "App stayed" listed '^App 1.0$'
tui 'o' "$W/nothere" '\r' '\x1b' 'q' > "$W/o6"
t "a wrong path is named, the prompt stays" grep -q \
    "no modlist or file" "$W/o6"
mkdir -p "$W/somedir"
tui 'o' "$W/somedir" '\r' '\x1b' 'q' > "$W/o7"
t "a directory without CKAN/ is explained" grep -q "is a directory" \
    "$W/o7"
mkdir -p "$W/oldgame/CKAN"
printf '%s\n' '{"installed_modules":{"Other":{"source_module":' \
    '{"identifier":"Other","version":"1.0"},"auto_installed":false}}}' \
    > "$W/oldgame/CKAN/registry.json"
tui 'o' "$W/oldgame" '\r' 'a' 'q' > "$W/o8"
t "a game folder with CKAN/ imports its registry" grep -q \
    "CKAN registry, 1 mods" "$W/o8"
tui 'C' 'q' > /dev/null

echo "== export"
tui 'w' '\x15' "$W/exp.txt" '\r' 'q' > "$W/o9"
t "w writes the chosen mods to a path" sh -c \
    "grep -q '^App 1.0$' '$W/exp.txt' && grep -q '^Base 1.0$' \
    '$W/exp.txt' && ! grep -q '^Lib' '$W/exp.txt'"
mkdir -p "$W/expdir"
tui 'w' '\x15' "$W/expdir/" '\r' 'q' > /dev/null
t "a directory gets a file in it" test -f "$W/expdir/modlist.kspkan"
tui 'w' '\x15' 'mine' '\r' 'q' > /dev/null
t "a plain name is a saved modlist" test -f "$S/modlists/mine"
tui 'w' '\x15' 'bad name' '\r' 'q' > "$W/o10"
t "a name with a space is refused" grep -q "letters, digits" "$W/o10"
kk remove App Base > /dev/null 2>&1
kk modlist install "$W/exp.txt" > /dev/null 2>&1
t "the exported file installs App again" listed '^App 1.0$'
t "and Base" listed '^Base 1.0$'

echo "== the mouse"
tui 'click 2 3' 'q' > /dev/null
t "a click in the mark column marks" sh -c "[ -s '$S/marks' ]"
tui 'click 2 3' 'q' > /dev/null
t "a second click there unmarks" test ! -e "$S/marks"
tui 'rclick 20 4' 'q' > /dev/null
t "a right click marks" sh -c "[ -s '$S/marks' ]"
tui 'C' 'dclick 30 5' 'q' > /dev/null
t "a double click marks" sh -c "[ -s '$S/marks' ]"
tui 'C' 'wheel down 30 5' 'wheel up 30 5' 'q' > "$W/o11"
t "the wheel moves through the list" grep -q " 4 of 5 " "$W/o11"
tui 'click 113 30' 'q' > "$W/o13"
t "a click on a key in the bottom bar works as the key" \
    grep -q "kspkan keys" "$W/o13"

echo "== games found on their own"
gog="$W/home/GOG Games/Kerbal Space Program/game"
mkdir -p "$gog/GameData/Squad" "$W/home/KSP-copy/GameData/Squad" \
    "$W/home/KSP-copy/GameData/SquadExpansion/MakingHistory" \
    "$W/home/KSP-copy/GameData/SomeMod" "$W/home/notgame/GameData"
printf 'Version 1.12.5\n' > "$gog/readme.txt"
"$KK" instances > "$W/o14" 2>&1
t "instances finds the GOG game" grep -q "^. gog .*GOG Games" "$W/o14"
t "and the copy in home" grep -q "KSP-copy" "$W/o14"
t "not a folder that only has GameData" sh -c \
    "! grep -q notgame '$W/o14'"
t "with the DLC and the version" sh -c \
    "grep -q 'Making History' '$W/o14' && grep -q 'game 1.12.5' '$W/o14'"
t "and the folders in GameData" grep -q "1 folders in GameData" "$W/o14"
D=$XDG_DATA_HOME/kspkan
for f in index summary provides counts meta; do
    cp -r "$S/$f" "$D/$f"
done
python3 -I "$W/drive.py" "$KK" -y -- '\t' 'j' '\r' 'q' > "$W/o15"
t "Tab lists the games" grep -q "kspkan games" "$W/o15"
t "Enter switches to one" grep -q "now KSP-copy" "$W/o15"
python3 -I "$W/drive.py" "$KK" -y -- '\t' 'dclick 5 4' 'q' \
    > "$W/o16"
t "a double click on a game switches too" grep -q "now gog" "$W/o16"
"$KK" forget KSP-copy > /dev/null
"$KK" instances > "$W/o17" 2>&1
t "a forgotten game is not added back" sh -c \
    "! grep -q KSP-copy '$W/o17'"
"$KK" add copy "$W/home/KSP-copy" > /dev/null
"$KK" instances > "$W/o18" 2>&1
t "kspkan add brings it back" grep -q "copy .*KSP-copy" "$W/o18"
t "the terminal is left as it was" sh -c \
    "tail -c 300 '$W/o15' | grep -qv 'kspkan games'"

kill $SRV 2>/dev/null
echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
