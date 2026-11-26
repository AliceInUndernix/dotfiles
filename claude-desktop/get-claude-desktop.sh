#!/bin/sh
# Fetch Anthropic's newest Claude desktop .deb, check it the way apt
# does (signed index, pinned key fingerprint, SHA256), and unpack it
# into ~/.system/claude-desktop/app. Run as your user, not root.
# Run it again to update.
set -e
repo=https://downloads.claude.ai/claude-desktop/apt/stable
key=https://downloads.claude.ai/claude-desktop/key.asc
fpr=31DDDE24DDFAB679F42D7BD2BAA929FF1A7ECACE
dir=$HOME/.system/claude-desktop
tmp=$dir/download.tmp

mkdir -p "$dir" "$HOME/.system/bin"
rm -rf "$tmp"
mkdir "$tmp"
trap 'rm -rf "$tmp"' EXIT

curl -fsSL -o "$tmp/key.asc" "$key"
curl -fsSL -o "$tmp/InRelease" "$repo/dists/stable/InRelease"
export GNUPGHOME="$tmp/gnupg"
mkdir -m 700 "$GNUPGHOME"
gpg -q --import "$tmp/key.asc" 2>/dev/null
gpg --with-colons --fingerprint | grep -q "^fpr:::::::::$fpr:" || {
    echo "signing key is not $fpr: stopping" >&2; exit 1; }
gpg -q --status-fd 1 --verify "$tmp/InRelease" 2>/dev/null |
    grep -q "VALIDSIG $fpr" || {
    echo "index not signed by Anthropic's key: stopping" >&2; exit 1; }
gpg -q --decrypt "$tmp/InRelease" > "$tmp/Release" 2>/dev/null
gpgconf --kill all 2>/dev/null || :

p=main/binary-amd64/Packages
want=$(awk -v p="$p" '/^SHA256:/ {s=1; next} /^[^ ]/ {s=0}
    s && $3 == p {print $1}' "$tmp/Release")
curl -fsSL -o "$tmp/Packages" "$repo/dists/stable/$p"
[ -n "$want" ] &&
    [ "$(sha256sum < "$tmp/Packages" | cut -d' ' -f1)" = "$want" ] || {
    echo "package list does not match signed index: stopping" >&2; exit 1; }
set -- $(awk '/^Package:/ {p=$2}
    p == "claude-desktop" && /^Version:/ {v=$2}
    p == "claude-desktop" && /^Filename:/ {f=$2}
    p == "claude-desktop" && /^SHA256:/ {h=$2}
    /^$/ {if (f) print v, f, h; p = f = ""}
    END {if (f) print v, f, h}' "$tmp/Packages" | sort -V | tail -n 1)
[ $# -eq 3 ] || { echo "no claude-desktop in the index" >&2; exit 1; }
ver=$1 file=$2 sum=$3

if [ "$(cat "$dir/app/.release" 2>/dev/null)" = "$ver" ]; then
    echo "already have Claude desktop $ver"
else
    echo "fetching Claude desktop $ver"
    q=; [ -t 2 ] || q=-sS   # a progress bar only on a terminal
    curl -fL $q -o "$tmp/c.deb" "$repo/$file"
    [ "$(sha256sum < "$tmp/c.deb" | cut -d' ' -f1)" = "$sum" ] || {
        echo "the .deb does not match its SHA256: stopping" >&2; exit 1; }
    (cd "$tmp" && ar x c.deb)
    mkdir "$tmp/app"
    tar -xf "$tmp"/data.tar.* -C "$tmp/app"
    echo "$ver" > "$tmp/app/.release"
    rm -rf "$dir/app"
    mv "$tmp/app" "$dir/app"
    echo "installed Claude desktop $ver in $dir/app"
fi

cat > "$HOME/.system/bin/claude-desktop" <<'L'
#!/bin/sh
# Uses the kernel's user namespaces as its sandbox (chrome-sandbox
# can't be setuid in your home). Never started with --no-sandbox.
app=$HOME/.system/claude-desktop/app/usr/lib/claude-desktop
exec "$app/claude-desktop" "$@"
L
chmod 755 "$HOME/.system/bin/claude-desktop"
