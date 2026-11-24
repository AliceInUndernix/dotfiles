#!/bin/sh
# Tests clean: what goes, that pictures and sound stay the same, the
# command line (-n, -H, -p, -b, -u, -o, -), and a fuzz run over broken
# files. Run from clean/: make test (or sh test/run.sh ./clean-bin).
# Sample files are made with ImageMagick (convert) and ffmpeg; formats
# whose tool is missing are skipped. Works in a directory of its own
# and removes it at the end.

set -u
CLEAN=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
FUZZ=${FUZZ:-200}
T=$(mktemp -d "${TMPDIR:-/tmp}/cleantest.XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
cd "$T" || exit 1
XDG_STATE_HOME=$T/state
export XDG_STATE_HOME
BK=$T/state/clean

FAILS=0
N=0
ok()   { N=$((N + 1)); printf 'ok   %s\n' "$*"; }
bad()  { N=$((N + 1)); FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
skip() { printf 'skip %s\n' "$*"; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }
sum()  { cksum < "$1"; }
has()  { command -v "$1" >/dev/null 2>&1; }

# what a picture or sound is, decoded
pix()  { convert "$1" rgba:- 2>/dev/null | cksum; }
snd()  { ffmpeg -v error -i "$1" -map 0:a -f s16le - 2>/dev/null | cksum; }

# --- samples: each has the word SECRETTAG in its metadata
S=SECRETTAG
if has convert; then
	convert -size 32x32 gradient:red-blue -set comment "$S" \
		-set date:create 2001-01-01 s.jpg
	convert -size 32x32 gradient:red-blue -set comment "$S" s.png
	convert -size 32x32 gradient:red-blue -set comment "$S" s.gif
	convert -size 32x32 gradient:red-blue -set comment "$S" s.tif
fi
if has ffmpeg; then
	for f in mp3 flac ogg wav m4a mkv; do
		ffmpeg -v error -f lavfi -i sine=frequency=440:duration=1 \
			-metadata title="$S" -metadata artist="$S" s.$f
	done
	ffmpeg -v error -f lavfi -i testsrc=size=64x48:duration=1 \
		-f lavfi -i sine=duration=1 -shortest \
		-metadata title="$S" -c:v mpeg4 -c:a aac s.mp4
fi
ls s.* >/dev/null 2>&1 || { echo "no samples: neither convert nor ffmpeg"; exit 1; }

# --- what goes, and what stays
for f in s.*; do
	cp "$f" "w-$f"
	case $f in *.jpg|*.png|*.gif|*.tif) kind=pix ;; *) kind=snd ;; esac
	before=$($kind "w-$f")
	"$CLEAN" -q "w-$f" 2>err
	check "$f: cleaned, no tag left" '[ $? -eq 0 ] && ! grep -q "$S" "w-$f"'
	check "$f: the picture or sound is the same" '[ "$($kind "w-$f")" = "$before" ]'
	"$CLEAN" "w-$f" > o.txt
	check "$f: again: nothing to remove" 'grep -q "nothing to remove" o.txt'
done

# --- the command line
f=$(ls s.* | head -n 1)
rm -rf "$T/state"
"$CLEAN" -h > o.txt 2>err
check "-h: help on stdout, exit 0" '[ $? -eq 0 ] && grep -q "^usage: clean" o.txt && [ ! -s err ]'
"$CLEAN" --help > o.txt 2>&1
check "--help works too" '[ $? -eq 0 ] && grep -q -- "-H" o.txt'
"$CLEAN" -x "$f" 2>err
check "an unknown option: exit 2, named" '[ $? -eq 2 ] && grep -q "unknown option -x" err'
"$CLEAN" -s "$f" 2>err
check "the old -s says it is now -H" '[ $? -eq 2 ] && grep -q "now -H" err'
"$CLEAN" -k "$f" 2>err
check "the old -k says it is now -p" '[ $? -eq 2 ] && grep -q "now -p" err'
"$CLEAN" 2>err
check "no FILE: said, exit 2" '[ $? -eq 2 ] && grep -q "give the files" err'

cp "$f" a; old=$(sum a)
"$CLEAN" -n a > o.txt
check "-n: says what would go, changes nothing" '[ "$(sum a)" = "$old" ] && grep -q "would remove" o.txt && [ ! -d "$BK" ]'
"$CLEAN" -q a
check "a backup is made" '[ "$(ls "$BK" | grep -vc list)" -eq 1 ]'
check "the times become 2000-01-01" '[ "$(date -u -r a +%Y%m%d%H%M)" = 200001010000 ]'
"$CLEAN" -q -H a; h1=$(sum a)
"$CLEAN" -q -H a
check "-H: the same hash on each run" '[ "$(sum a)" = "$h1" ]'
"$CLEAN" -q a
check "without -H: a new hash" '[ "$(sum a)" != "$h1" ]'
"$CLEAN" -q -u a
check "-u puts the newest backup back" '[ "$(sum a)" = "$h1" ]'
"$CLEAN" -q -u a
check "-u again goes further back, to the original" '[ "$(sum a)" = "$old" ]'
"$CLEAN" -u a 2>err
check "-u: no backup left: said" '[ $? -eq 1 ] && grep -q "no backup" err'
check "-u removes each backup it puts back" '[ "$(ls "$BK" | grep -vc list)" -eq 0 ]'

cp "$f" b; touch -d 2010-05-05 b
"$CLEAN" -q -b -p b
check "-b: no backup; -p: the times stay" '[ "$(ls "$BK" | grep -vc list)" -eq 0 ] && [ "$(date -r b +%Y)" = 2010 ]'

cp "$f" c; old=$(sum c)
"$CLEAN" -q -o c2 c
check "-o: a clean copy, the file stays" '[ "$(sum c)" = "$old" ] && ! grep -q "$S" c2 && [ "$(ls "$BK" | grep -vc list)" -eq 0 ]'
"$CLEAN" -q -o c2 c 2>err
check "-o won't write over a file" '[ $? -eq 1 ] && grep -q "already exists" err'
"$CLEAN" -o c3 c d 2>err
check "-o takes one FILE" '[ $? -eq 2 ] && [ ! -e c3 ]'
"$CLEAN" - < c > c4 2>err
check "-: standard input to output, notes on stderr" '[ $? -eq 0 ] && ! grep -q "$S" c4 && [ -s c4 ] && grep -q "^-: removed" err'
echo hello | "$CLEAN" - > c5 2>err
check "-: a file it doesn't know: exit 1, nothing out" '[ $? -eq 1 ] && [ ! -s c5 ]'
echo hello > txt
"$CLEAN" txt 2>err
check "a file it doesn't know: left, exit 1" '[ $? -eq 1 ] && [ "$(cat txt)" = hello ] && grep -q "left as it was" err'
"$CLEAN" . 2>err
check "a directory: said how" '[ $? -eq 1 ] && grep -q "find DIR" err'
cp "$f" ./-d
"$CLEAN" -q -- -d
check "-- ends the options" '[ $? -eq 0 ] && ! grep -q "$S" ./-d'

# --- fuzz: broken copies of every sample must never crash it
crash=0
i=0
for f in s.*; do
	size=$(wc -c < "$f")
	j=0
	while [ "$j" -lt "$FUZZ" ]; do
		cp "$f" fz
		# a few random bytes at random places, or a cut
		k=0
		while [ "$k" -lt 4 ]; do
			at=$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')
			at=$((at % size))
			head -c 1 /dev/urandom | dd of=fz bs=1 seek="$at" conv=notrunc 2>/dev/null
			k=$((k + 1))
		done
		if [ $((j % 10)) -eq 0 ]; then
			at=$(od -An -N4 -tu4 /dev/urandom | tr -d ' ')
			head -c $((at % size)) "$f" > fz
		fi
		timeout 20 "$CLEAN" -q -b fz 2>/dev/null
		rc=$?
		if [ "$rc" -gt 1 ]; then
			crash=$((crash + 1))
			cp fz "$T/../clean-crash-$i-$f" 2>/dev/null
		fi
		j=$((j + 1))
		i=$((i + 1))
	done
done
check "fuzz: $i broken files, no crash or hang" '[ "$crash" -eq 0 ]'

echo
if [ "$FAILS" -eq 0 ]; then
	echo "all $N tests passed"
	exit 0
fi
echo "$FAILS of $N tests failed"
exit 1
