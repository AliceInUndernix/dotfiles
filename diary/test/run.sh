#!/bin/sh
# Tests diary without its screen (diary -s -c): making, opening,
# saving, wrong passwords, what is left in RAM, crashes, and that
# unseal opens a diary too. Run from diary/: make test. Works in
# directories of its own and removes them at the end.

set -u
DIARY=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
SEAL=$(cd "$(dirname "$DIARY")/../seal" && pwd)/seal
T=$(mktemp -d "${TMPDIR:-/tmp}/diarytest.XXXXXX") || exit 1
RAMT=$(mktemp -d /dev/shm/diarytest.XXXXXX) || exit 1
HOME=$T/home
XDG_RUNTIME_DIR=$RAMT
XDG_CONFIG_HOME=$T/config
export HOME XDG_RUNTIME_DIR XDG_CONFIG_HOME
mkdir -p "$HOME"
trap 'chmod -R u+rwx "$T" "$RAMT" 2>/dev/null; rm -rf "$T" "$RAMT"' EXIT
cd "$T" || exit 1
RAM=$RAMT/diary

FAILS=0
N=0
ok()   { N=$((N + 1)); printf 'ok   %s\n' "$*"; }
bad()  { N=$((N + 1)); FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

d() { echo "${PW-pw}" | "$DIARY" -s "$@"; }

d -n -c 'echo "Walk with @mira" > days/2026-11-21;
	mkdir -p people/mira; echo "name: Mira" > people/mira/note'
check "diary -n makes ~/.diary" '[ -f "$HOME/.diary" ]'
check "it is only yours" '[ "$(stat -c %a "$HOME/.diary")" = 600 ]'
check "nothing readable in it" \
	'! grep -q -e mira -e Walk -e days "$HOME/.diary"'
check "nothing left in RAM" '[ -z "$(ls -A "$RAM" 2>/dev/null)" ]'
check "nothing left next to it" '[ "$(ls -A "$HOME" | wc -l)" = 2 ]'

d -n -c true 2>/dev/null
check "diary -n won't overwrite a diary" '[ $? -eq 1 ]'

out=$(d -c 'cat days/2026-11-21; ls -d days people places events 2>&1')
check "it opens with the password" 'echo "$out" | grep -q "Walk with @mira"'
check "the directories are there" 'echo "$out" | grep -q places'

PW=wrong d -c 'echo opened' > out 2>&1
check "a wrong password opens nothing" '[ $? -eq 1 ] && ! grep -qx opened out'
check "a wrong password leaves nothing in RAM" \
	'[ -z "$(ls -A "$RAM" 2>/dev/null)" ]'

cp "$HOME/.diary" before
d -c 'echo more >> days/2026-11-21'
check "a change is saved" '! cmp -s before "$HOME/.diary"'
check "the old one is kept as .diary.old" 'cmp -s before "$HOME/.diary.old"'
out=$(d -c 'cat days/2026-11-21')
check "the change is in it" 'echo "$out" | grep -q more'

cp "$HOME/.diary" before
d -c true
check "no change, no new file" 'cmp -s before "$HOME/.diary"'

d -c 'exit 3'
check "-c gives CMD's exit status" '[ $? -eq 3 ]'

d -c 'echo lost > days/2026-11-22; kill -9 $PPID' 2>/dev/null
check "killed: the diary stays in RAM" \
	'[ -f "$RAM/diary/days/2026-11-22" ]'
d -c true > out 2>&1
check "next time it says so and stops (no terminal)" \
	'[ $? -eq 1 ] && grep -q "wasn.t closed" out'
rm -rf "$RAM"

d -c "echo pw | '$DIARY' -s -c true" > out 2>&1
check "a second diary while one is open stops" \
	'grep -q "open already" out'
mkdir -p "$RAM"
echo 1 > "$RAM/pid"
d -c true 2>/dev/null
check "a stale pid of another program doesn't stop it" '[ $? -eq 0 ]'
rm -rf "$RAM"

d -c 'mkdir -p people/old/notes people/old/files;
	echo x > days/2026-01-02.txt; echo x > people/old/note.txt;
	echo x > people/old/notes/looks.txt; echo x > people/old/files/a.txt'
out=$(d -c 'ls days people/old people/old/notes people/old/files')
check "old .txt names lose .txt, attachments keep theirs" \
	'echo "$out" | grep -qx 2026-01-02 && echo "$out" | grep -qx note &&
	echo "$out" | grep -qx looks && echo "$out" | grep -qx a.txt'

if [ -x "$SEAL" ]; then
	mkdir u
	cp "$HOME/.diary" u/x
	(cd u && echo pw | "$SEAL" -u -s -q -d . x)
	check "unseal opens a diary too" '[ -f u/diary/days/2026-11-21 ]'
else
	printf 'skip unseal opens a diary too (no %s: make -C ../seal)\n' "$SEAL"
fi

head -c 100 "$HOME/.diary" > cut
d -f cut -c true 2>/dev/null
check "a damaged file opens nothing" '[ $? -eq 1 ]'

d -x 2>/dev/null
check "a wrong option: exit 2" '[ $? -eq 2 ]'
"$DIARY" < /dev/null > /dev/null 2>&1
check "the screen needs a terminal: exit 2" '[ $? -eq 2 ]'

echo
if [ "$FAILS" -eq 0 ]; then
	echo "all $N tests passed"
else
	echo "$FAILS of $N tests failed"
	exit 1
fi
