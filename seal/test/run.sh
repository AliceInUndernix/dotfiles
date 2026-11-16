#!/bin/sh
# Tests seal and unseal: round trips, wrong passwords, damage, links,
# odd names, big trees. Run from seal/: make test (or sh test/run.sh
# ./seal). Works in a directory of its own and removes it at the end;
# RAM unseals go to a tmpfs directory of its own too.

set -u
SEAL=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d "${TMPDIR:-/tmp}/sealtest.XXXXXX") || exit 1
RAMT=$(mktemp -d /dev/shm/sealtest.XXXXXX) || exit 1
XDG_RUNTIME_DIR=$RAMT
export XDG_RUNTIME_DIR
RAM=$RAMT/seal
trap 'chmod -R u+rwx "$T" "$RAMT" 2>/dev/null; rm -rf "$T" "$RAMT"' EXIT
cd "$T" || exit 1
ln -s "$SEAL" unseal
UNSEAL=$T/unseal

FAILS=0
N=0
ok()   { N=$((N + 1)); printf 'ok   %s\n' "$*"; }
bad()  { N=$((N + 1)); FAILS=$((FAILS + 1)); printf 'FAIL %s\n' "$*"; }
check() { if eval "$2"; then ok "$1"; else bad "$1"; fi; }

seal()   { echo "${PW-pw}" | "$SEAL" -s -q "$@"; }
unseal() { echo "${PW-pw}" | "$UNSEAL" -s -q "$@"; }

# what a tree is: types, modes, link targets and contents, sorted
tree() {
	( cd "$1" && find . -printf '%y %m %p -> %l\n' | LC_ALL=C sort &&
		find . -type f -exec cksum {} + | LC_ALL=C sort )
}

mkfile() { head -c "$2" /dev/urandom > "$1"; }

# --- a tree with all kinds of things
mk_tree() {
	mkdir -p "$1/sub/deep/deeper" "$1/empty" "$1/-dash" "$1/with space"
	echo hello > "$1/a.txt"
	: > "$1/zero"
	mkfile "$1/sub/c64k" 65536
	mkfile "$1/sub/c64k-1" 65535
	mkfile "$1/sub/c64k+1" 65537
	mkfile "$1/sub/c128k" 131072
	mkfile "$1/sub/deep/deeper/odd" 100003
	printf 'x' > "$1/with space/name with space"
	printf 'y' > "$1/$(printf 'new\nline')"
	printf 'z' > "$1/$(printf 'caf\351')"
	ln -s a.txt "$1/rel-link"
	ln -s /etc/passwd "$1/abs-link"
	ln -s nowhere "$1/dangling"
	ln -s sub "$1/dir-link"
	chmod 750 "$1/sub"
	chmod 700 "$1/sub/deep"
	chmod 755 "$1/a.txt"
	chmod 400 "$1/zero"
	chmod 555 "$1/-dash"
}

mk_tree baba
tree baba > want
seal baba
check "seal makes baba.seal and removes baba" '[ -f baba.seal ] && [ ! -e baba ]'
check "the sealed file is only yours" '[ "$(stat -c %a baba.seal)" = 600 ]'
check "no trace of its name in the sealed file" '! grep -q baba baba.seal && ! grep -q hello baba.seal'

unseal -t baba.seal > list
check "unseal -t checks it" '[ $? -eq 0 ] && grep -q "^baba/sub/c64k$" list'
unseal -d . baba.seal
check "unseal -d . gives it back and removes the file" '[ -d baba ] && [ ! -e baba.seal ]'
tree baba > got
check "the tree is the same (types, modes, links, contents)" 'cmp -s want got'

# --- seal into RAM and back
seal -o holiday.mp4 baba
check "seal -o holiday.mp4" '[ -f holiday.mp4 ] && [ ! -e baba ]'
unseal holiday.mp4
check "unseal into RAM keeps the sealed file" '[ -d "$RAM/baba" ] && [ -f holiday.mp4 ]'
check "the RAM directory is a tmpfs" '[ "$(stat -f -c %T "$RAM")" = tmpfs ]'
tree "$RAM/baba" > got
check "the tree in RAM is the same" 'cmp -s want got'
unseal holiday.mp4 2>err
check "a second unseal into RAM is refused" '[ $? -ne 0 ] && grep -q "already unsealed" err'
"$SEAL" -l > list
check "seal -l lists it" 'grep -q "holiday.mp4" list'
echo changed > "$RAM/baba/a.txt"
mkdir "$RAM/baba/new"
old=$(cksum < holiday.mp4)
seal "$RAM/baba"
check "seal puts it back into holiday.mp4" '[ ! -e "$RAM/baba" ] && [ -f holiday.mp4 ] && [ "$(cksum < holiday.mp4)" != "$old" ] && [ ! -e baba.seal ]'
check "and forgets it was unsealed" '[ ! -e "$RAM/.from/baba" ]'
unseal -d . holiday.mp4
check "the change is in it" '[ "$(cat baba/a.txt)" = changed ] && [ -d baba/new ]'
rmdir baba/new
echo hello > baba/a.txt
chmod 755 baba/a.txt
tree baba > got
check "and the rest is as it was" 'cmp -s want got'

# --- passwords
seal baba
cp baba.seal keep.seal
PW=wrong unseal -d . baba.seal 2>err
check "a wrong password is refused" '[ $? -ne 0 ] && grep -q "wrong password" err'
check "and nothing is made or removed" '[ ! -e baba ] && cmp -s baba.seal keep.seal && [ -z "$(ls -A "$RAM" | grep -v "^.from$")" ]'
PW=wrong unseal baba.seal 2>/dev/null
check "a wrong password leaves nothing in RAM" '[ ! -e "$RAM/baba" ] && [ -z "$(ls -A "$RAM" | grep -v "^.from$")" ]'
unseal -d . baba.seal
PW= seal -o empty.seal baba 2>err
check "an empty password is refused" '[ $? -ne 0 ] && grep -q "no password" err && [ ! -e empty.seal ] && [ -d baba ]'
seal baba
seal -k -o other.seal --  "$T/no-such" 2>/dev/null
check "a missing directory is refused" '[ $? -ne 0 ] && [ ! -e other.seal ]'

# --- damage
size=$(stat -c %s baba.seal)
flip() {   # file offset: change one byte
	cp keep.seal "$1"
	printf '\001' | dd of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null
}
flip t1.seal 5                        # the salt
flip t2.seal 200000                   # inside a later chunk
flip t3.seal $((size - 3))            # the last mac
for f in t1 t2 t3; do
	unseal -d . $f.seal 2>err
	check "a changed byte is found ($f)" '[ $? -ne 0 ] && [ ! -e baba ] && [ -f $f.seal ] && [ -z "$(ls -A | grep "^\.seal-")" ]'
done
check "and said where" 'grep -q "changed or damaged near byte" err'
unseal -f -d . t2.seal 2>err
check "-f unseals a damaged file anyway, and keeps the file" '[ $? -ne 0 ] && [ -d baba ] && [ -f t2.seal ] && grep -q "going on anyway" err'
tree baba > got
check "-f: only the damaged part differs" '[ "$(diff want got | grep -c "^>")" -le 1 ]'
chmod -R u+rwx baba
rm -rf baba
head -c $((16 + 65552)) keep.seal > cut1.seal       # at a chunk's end
head -c $((size - 1)) keep.seal > cut2.seal         # one byte short
cp keep.seal long.seal
printf 'x' >> long.seal                             # one byte more
# two chunks swapped
dd if=keep.seal of=sw.seal bs=1 count=16 2>/dev/null
dd if=keep.seal bs=1 skip=$((16 + 65552)) count=65552 2>/dev/null >> sw.seal
dd if=keep.seal bs=1 skip=16 count=65552 2>/dev/null >> sw.seal
dd if=keep.seal bs=1 skip=$((16 + 2 * 65552)) 2>/dev/null >> sw.seal
for f in cut1 cut2 long sw; do
	unseal -d . $f.seal 2>/dev/null
	check "cut, added to or reordered is found ($f)" '[ $? -ne 0 ] && [ ! -e baba ]'
done
head -c 20 keep.seal > tiny.seal
unseal -d . tiny.seal 2>err
check "a tiny file is not a sealed one" '[ $? -ne 0 ] && grep -q "too short" err'
mkfile random.mp4 100000
unseal -d . random.mp4 2>err
check "random bytes are not a sealed file" '[ $? -ne 0 ] && grep -q "wrong password, or it is not" err'

unseal -d . baba.seal
rm -f t?.seal cut?.seal long.seal sw.seal tiny.seal random.mp4 keep.seal

# --- refusals that change nothing
seal -o baba/inside.seal baba 2>err
check "the sealed file can't be inside the directory" '[ $? -ne 0 ] && [ -d baba ] && [ ! -e baba/inside.seal ] && [ -z "$(ls -A baba | grep "^\.inside")" ]'
tree baba > got
check "and baba is untouched" 'cmp -s want got'
: > exists.seal
seal -o exists.seal baba 2>err
check "an existing file is not overwritten" '[ $? -ne 0 ] && [ ! -s exists.seal ] && [ -d baba ]'
rm exists.seal
mkfifo baba/fifo
seal baba 2>err
check "a pipe in it: refused, nothing changed" '[ $? -ne 0 ] && [ -d baba ] && [ ! -e baba.seal ] && grep -q "pipe" err'
rm baba/fifo
ln -s baba babalink
seal babalink 2>err
check "a link to a directory is refused" '[ $? -ne 0 ] && [ -d baba ] && grep -q "symbolic link" err'
rm babalink
(cd baba && seal . ) 2>err
check "seal . seals the directory you are in" '[ -f baba.seal ] && [ ! -e baba ]'
unseal -d . baba.seal
seal -k baba
check "-k keeps the directory" '[ -d baba ] && [ -f baba.seal ]'
unseal -d . baba.seal 2>err
check "unseal -d won't write over a directory" '[ $? -ne 0 ] && grep -q "already exists" err && [ -f baba.seal ]'
mkdir out
unseal -k -d out baba.seal
check "unseal -k -d out keeps the file" '[ -d out/baba ] && [ -f baba.seal ]'
tree out/baba > got
check "and out/baba is the same" 'cmp -s want got'
seal -o a.seal baba
seal -o b.seal out/baba 2>/dev/null
check "two seals of the same tree differ" '! cmp -s a.seal b.seal'
unseal -d . a.seal
rm -f b.seal baba.seal

# --- a hard link: two copies
ln baba/a.txt baba/hard
seal baba
unseal -d . baba.seal
check "a hard link comes back as its own copy" '[ "$(cat baba/hard)" = hello ] && [ "$(stat -c %h baba/hard)" = 1 ]'
rm baba/hard

# --- something changed after sealing is not removed
# (simulated: the file is changed between verify and removal is not
# testable from outside; at least a directory gaining a file while
# sealing is left, which is what the list-driven removal does)

# --- a big tree and a big file
mkdir big
i=0
while [ $i -lt 40 ]; do
	mkdir big/d$i
	j=0
	while [ $j -lt 50 ]; do
		echo "$i $j" > big/d$i/f$j
		j=$((j + 1))
	done
	i=$((i + 1))
done
mkfile big/large 50000000
tree big > want2
t0=$(date +%s)
seal big
unseal -d . big.seal
t1=$(date +%s)
tree big > got2
check "2000 files and 50 MB come back the same ($((t1 - t0)) s)" 'cmp -s want2 got2'

# --- the password from the terminal: twice, and they must match
if command -v script >/dev/null 2>&1; then
	rm -rf big
	mkdir tty; echo t > tty/f
	printf 'one\ntwo\n' | script -qc "$SEAL tty" /dev/null >/dev/null 2>&1
	check "two different passwords: nothing sealed" '[ -d tty ] && [ ! -e tty.seal ]'
fi

echo
if [ "$FAILS" -eq 0 ]; then
	echo "all $N tests passed"
	exit 0
fi
echo "$FAILS of $N tests failed"
exit 1
