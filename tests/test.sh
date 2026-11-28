#!/bin/sh
# kspkan tests.  Uses a scratch game directory; never touches a real game.
# usage: tests/test.sh [STATEDIR_WITH_LIST]
# STATEDIR_WITH_LIST must hold index, summary, provides, counts and meta/
# (as made by "kspkan update").  Downloads a few small mods.

KK=${KK:-$(cd "$(dirname "$0")/.." && pwd)/kspkan}
LIST=${1:-${XDG_DATA_HOME:-$HOME/.local/share}/kspkan}
if [ ! -e "$LIST/index" ]; then
    echo "no module list in $LIST: run  kspkan update  first" >&2
    echo "(or give the folder that holds it as the first argument)" >&2
    exit 2
fi
W=${TMPDIR:-/tmp}/kspkan-test.$$
G=$W/game
S=$W/state
PASS=0
FAIL=0

mkdir -p "$G/GameData" "$S"
for f in index summary provides counts meta; do
    ln -s "$LIST/$f" "$S/$f"
done
echo "scratch: $W"

kk() { "$KK" -g "$G" -s "$S" -k 1.12.5 -y "$@"; }
reg() { kk list 2>/dev/null; }
files() { find "$G/GameData" -type f | wc -l | tr -d ' '; }
dirs() { find "$G/GameData" -mindepth 1 | wc -l | tr -d ' '; }

check() {
    # check "description" command...
    desc=$1; shift
    if "$@" >/dev/null 2>&1; then
        PASS=$((PASS + 1)); echo "ok    $desc"
    else
        FAIL=$((FAIL + 1)); echo "FAIL  $desc"
    fi
}
fails() {
    desc=$1; shift
    if "$@" >/dev/null 2>&1; then
        FAIL=$((FAIL + 1)); echo "FAIL  $desc (should have failed)"
    else
        PASS=$((PASS + 1)); echo "ok    $desc"
    fi
}
has_line() { reg | grep -q "$1"; }
no_line() { ! reg | grep -q "$1"; }
empty_game() { [ "$(dirs)" = 0 ]; }

echo "== 1. dependency is installed with its parent"
check "install MechJeb2" kk install MechJeb2
check "MechJeb2 listed" has_line '^MechJeb2 '
check "ModuleManager listed as dependency" has_line '^ModuleManager .*(dependency)'
check "MechJeb2 plugin file exists" test -f "$G/GameData/MechJeb2/Plugins/MechJeb2.dll"
check "ModuleManager dll exists" sh -c "ls '$G'/GameData/ModuleManager.*.dll"
echo "== 2. installing again does nothing"
out=$(kk install MechJeb2 2>&1)
check "says nothing to do" sh -c "echo '$out' | grep -q 'nothing to do'"
echo "== 3. removing the parent removes the unneeded dependency"
check "remove MechJeb2" kk remove MechJeb2
check "registry empty" sh -c "[ -z \"\$(\"$KK\" -g '$G' -s '$S' -k 1.12.5 list)\" ]"
check "GameData empty again, no leftover folders" empty_game
echo "== 4. an explicitly installed dependency stays"
check "install ModuleManager" kk install ModuleManager
check "install MechJeb2" kk install MechJeb2
check "ModuleManager is not marked dependency" no_line '^ModuleManager .*(dependency)'
check "remove MechJeb2" kk remove MechJeb2
check "ModuleManager still installed" has_line '^ModuleManager '
check "its dll is still there" sh -c "ls '$G'/GameData/ModuleManager.*.dll"
echo "== 5. removing a dependency removes what needs it"
check "install MechJeb2" kk install MechJeb2
check "remove ModuleManager" kk remove ModuleManager
check "MechJeb2 went too" no_line '^MechJeb2 '
check "GameData empty again" empty_game
echo "== 6. files not installed by kspkan are protected"
echo mine > "$G/GameData/ModuleManager.4.2.3.dll"
fails "install refuses to overwrite" kk install ModuleManager
check "my file is unchanged" sh -c "[ \"\$(cat '$G'/GameData/ModuleManager.4.2.3.dll)\" = mine ]"
check "nothing registered after the refusal" sh -c "[ -z \"\$(\"$KK\" -g '$G' -s '$S' -k 1.12.5 list)\" ]"
check "with -f it overwrites" kk -f install ModuleManager
check "now it is the real dll" sh -c "[ \$(wc -c < '$G'/GameData/ModuleManager.4.2.3.dll) -gt 1000 ]"
check "remove it" kk remove ModuleManager
echo "== 7. a pinned version, then upgrade"
check "install ModuleManager=4.2.2" kk install ModuleManager=4.2.2
check "version 4.2.2 is listed" has_line '^ModuleManager 4.2.2'
check "upgrade" kk upgrade
check "now 4.2.3" has_line '^ModuleManager 4.2.3'
check "old dll is gone" sh -c "! ls '$G'/GameData/ModuleManager.4.2.2.dll"
check "new dll is there" test -f "$G/GameData/ModuleManager.4.2.3.dll"
check "remove" kk remove ModuleManager
echo "== 8. a damaged download is detected and fetched again"
check "install ModuleManager" kk install ModuleManager
check "remove it" kk remove ModuleManager
echo junk >> "$S"/cache/ModuleManager-4.2.3.zip
check "install again repairs the cache" kk install ModuleManager
check "dll has the right content" sh -c "[ \$(wc -c < '$G'/GameData/ModuleManager.4.2.3.dll) = 141824 ]"
check "remove" kk remove ModuleManager
echo "== 9. conflicting modules"
check "install AntennaHelper" kk install AntennaHelper
fails "AntennaHelperNext conflicts with it" kk install AntennaHelperNext
check "AntennaHelperNext was not installed" no_line '^AntennaHelperNext'
check "remove AntennaHelper" kk remove AntennaHelper
check "now AntennaHelperNext installs" kk install AntennaHelperNext
check "remove it" kk remove AntennaHelperNext
check "GameData empty again" empty_game
echo "== 10. unknown names and bad requests"
fails "unknown module" kk install NoSuchModuleAnywhere
fails "removing what is not installed" kk remove ModuleManager
fails "unknown version" kk install ModuleManager=9.9.9
echo "== 11. dependencies of several levels"
check "install Kopernicus" kk install Kopernicus
for d in ModuleManager Harmony2 ModularFlightIntegrator KSPTextureLoader; do
    check "dependency $d present" has_line "^$d "
done
check "remove Kopernicus" kk remove Kopernicus
check "all its dependencies went with it" empty_game

echo "== 12. recommended modules"
check "install Astrogator with -R" kk -R install Astrogator
check "its recommended ModuleManager came too" has_line "^ModuleManager "
check "ModuleManager counts as chosen, not as dependency" no_line "^ModuleManager .*(dependency)"
check "remove Astrogator" kk remove Astrogator
check "ModuleManager stays" has_line "^ModuleManager "
check "remove it" kk remove ModuleManager
echo "== 13. a pinned older version replaces a newer one"
check "install ModuleManager" kk install ModuleManager
check "pin 4.2.2" kk install ModuleManager=4.2.2
check "it is 4.2.2 now" has_line "^ModuleManager 4.2.2"
check "only one dll" sh -c "[ \$(ls '$G'/GameData/ModuleManager.*.dll | wc -l) = 1 ]"
check "remove" kk remove ModuleManager
check "GameData empty again" empty_game

echo
echo "$PASS passed, $FAIL failed"
[ "$KEEP" = 1 ] || rm -rf "$W"
[ "$FAIL" = 0 ]
