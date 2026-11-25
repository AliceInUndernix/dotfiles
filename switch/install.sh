#!/bin/sh
# Configure IceCat and install "switch" and the add-ons in addons.txt
# into an IceCat profile. Run as your ordinary user (not root), with
# IceCat closed.
#
# Usage: ./install.sh [-n] [-u] [-x] [-f] [-d] [profile_directory]
#   -n  dry run: show what would be done, change nothing
#   -u  uninstall: remove "switch" and the settings in icecat.prefs
#       (the add-ons from addons.txt stay; remove them in about:addons)
#   -x  skip the add-ons in addons.txt
#   -f  replace add-ons that are already installed
#   -d  fetch add-ons directly instead of through Tor
# Without a profile directory, the default IceCat profile is used.
# Set ICECAT_ROOT to use another IceCat directory, TOR_SOCKS to use
# another Tor address (default 127.0.0.1:9050).

set -e

ext_id="switch@example.org"
here=$(cd "$(dirname "$0")" && pwd)
src="$here/icecat-switch"
prefs_file="$here/icecat.prefs"
addons_file="$here/addons.txt"
chrome_file="$here/userChrome.css"
root="${ICECAT_ROOT:-$HOME/.mozilla/icecat}"
tor="${TOR_SOCKS:-127.0.0.1:9050}"
catalog="https://gnuzilla.gnu.org/extension.php?id="
max_size=52428800
dry=0
remove=0
addons=1
force=0
direct=0
tmp=""

die()
{
    echo "install.sh: $*" >&2
    exit 1
}

cleanup()
{
    if [ -n "$tmp" ]; then
        rm -f "$tmp/page" "$tmp/addon.xpi"
        rmdir "$tmp" 2> /dev/null || true
    fi
}

run()
{
    if [ "$dry" = 1 ]; then
        echo "would run: $*"
    else
        "$@"
    fi
}

default_profile()
{
    ini="$root/profiles.ini"
    [ -f "$ini" ] || die "no $ini; pass the profile directory"
    p=$(awk -F= '
        /^\[Install/ { ins = 1; next }
        /^\[/ { ins = 0 }
        ins && $1 == "Default" { print $2; exit }
    ' "$ini")
    if [ -z "$p" ]; then
        p=$(awk -F= '
            /^\[/ {
                if (d && p != "") { print p; found = 1; exit }
                p = ""
                d = 0
            }
            $1 == "Path" { p = $2 }
            $1 == "Default" && $2 == "1" { d = 1 }
            END { if (!found && d && p != "") print p }
        ' "$ini")
    fi
    [ -n "$p" ] || die "no default profile in $ini"
    case "$p" in
    /*) echo "$p" ;;
    *) echo "$root/$p" ;;
    esac
}

unset_pref()
{
    file="$prof/user.js"
    if [ "$dry" = 1 ]; then
        echo "would remove from $file: $1"
        return
    fi
    if [ -f "$file" ]; then
        grep -vF "\"$1\"" "$file" > "$file.tmp" || true
        mv "$file.tmp" "$file"
    fi
}

set_pref()
{
    line="user_pref(\"$1\", $2);"
    if [ "$dry" = 1 ]; then
        echo "would set: $line"
        return
    fi
    unset_pref "$1"
    echo "$line" >> "$prof/user.js"
}

all_prefs()
{
    [ -f "$prefs_file" ] || die "no $prefs_file"
    grep -v '^[[:space:]]*#' "$prefs_file" |
    while read -r name value; do
        [ -n "$name" ] || continue
        [ -n "$value" ] || die "no value for $name in $prefs_file"
        "$1" "$name" "$value"
    done
}

pack()
{
    if [ "$dry" = 1 ]; then
        echo "would pack $src into $xpi"
        return
    fi
    rm -f "$xpi"
    if command -v zip > /dev/null; then
        (cd "$src" && zip -q -X -r "$xpi" .)
    elif command -v bsdtar > /dev/null; then
        (cd "$src" && bsdtar -a -cf "$xpi" --format zip .)
    else
        die "need zip or bsdtar to pack the extension"
    fi
}

fetch()
{
    if [ "$direct" = 1 ]; then
        curl -fsSL --max-time 300 --max-filesize "$max_size" \
            -A "Mozilla/5.0" -o "$2" "$1"
    else
        curl -fsSL --max-time 300 --max-filesize "$max_size" \
            --socks5-hostname "$tor" -A "Mozilla/5.0" -o "$2" "$1"
    fi
}

# The add-on's id, from the manifest inside the xpi
addon_id()
{
    unzip -p "$1" manifest.json 2> /dev/null | tr -d '\n\r' |
    sed -n 's/.*"gecko"[^}]*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p'
}

install_addon()
{
    num="$1"
    name="$2"
    case "$num" in
    *[!0-9]*|"") echo "  skipped: bad number '$num'"; return ;;
    esac
    if [ "$dry" = 1 ]; then
        echo "would fetch $name from $catalog$num"
        return
    fi
    if ! fetch "$catalog$num" "$tmp/page"; then
        echo "  $name: cannot reach GNUzilla"
        return
    fi
    url=$(grep -o 'href="https://addons\.mozilla\.org/[^"]*\.xpi"' \
        "$tmp/page" | head -n 1 | sed 's/^href="//; s/"$//')
    if [ -z "$url" ]; then
        echo "  $name: no download on its GNUzilla page"
        return
    fi
    if ! fetch "$url" "$tmp/addon.xpi"; then
        echo "  $name: download failed (or larger than $max_size bytes)"
        return
    fi
    id=$(addon_id "$tmp/addon.xpi")
    case "$id" in
    ""|*[!A-Za-z0-9@._{}-]*)
        echo "  $name: not a valid add-on, left out"
        return
        ;;
    "$ext_id")
        echo "  $name: has the id of switch, left out"
        return
        ;;
    esac
    if [ -f "$prof/extensions/$id.xpi" ] && [ "$force" = 0 ]; then
        echo "  $name: already installed, kept (-f replaces it)"
        return
    fi
    cp "$tmp/addon.xpi" "$prof/extensions/$id.xpi"
    echo "  $name: installed ${url##*/}"
}

all_addons()
{
    [ -f "$addons_file" ] || die "no $addons_file"
    command -v curl > /dev/null || die "need curl to fetch add-ons"
    command -v unzip > /dev/null || die "need unzip to check add-ons"
    if [ "$dry" = 0 ]; then
        tmp=$(mktemp -d)
        trap cleanup EXIT INT TERM
    fi
    echo "Add-ons from GNUzilla:"
    grep -v '^[[:space:]]*#' "$addons_file" |
    while read -r num name; do
        [ -n "$num" ] || continue
        install_addon "$num" "$name"
    done
}

# A habamax look for IceCat's own window; never replaces yours
install_chrome()
{
    dest="$prof/chrome/userChrome.css"
    [ -f "$chrome_file" ] || return 0
    if [ -f "$dest" ] && ! grep -q "switch: habamax" "$dest"; then
        echo "kept your own $dest"
        return
    fi
    run mkdir -p "$prof/chrome"
    run cp "$chrome_file" "$dest"
}

remove_chrome()
{
    dest="$prof/chrome/userChrome.css"
    if [ -f "$dest" ] && grep -q "switch: habamax" "$dest"; then
        run rm -f "$dest"
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
    -n) dry=1 ;;
    -u) remove=1 ;;
    -x) addons=0 ;;
    -f) force=1 ;;
    -d) direct=1 ;;
    -*) die "unknown option $1" ;;
    *) prof="$1" ;;
    esac
    shift
done

[ "$(id -u)" != 0 ] || die "run this as your ordinary user, not root"
[ -f "$src/manifest.json" ] || die "no extension in $src"
[ -n "$prof" ] || prof=$(default_profile)
[ -d "$prof" ] || die "no profile directory $prof"

# IceCat holds a "lock" link to "address:+pid" while it uses a profile
in_use()
{
    t=$(readlink "$prof/lock" 2> /dev/null) || return 1
    pid=${t##*+}
    case "$pid" in
    ""|*[!0-9]*) return 1 ;;
    esac
    kill -0 "$pid" 2> /dev/null
}

if in_use; then
    if [ "$dry" = 1 ]; then
        echo "note: IceCat is running; close it before the real run"
    else
        die "close IceCat first"
    fi
fi

xpi="$prof/extensions/$ext_id.xpi"

if [ "$remove" = 1 ]; then
    run rm -f "$xpi"
    all_prefs unset_pref
    remove_chrome
    echo "Removed switch, its look and its settings from user.js."
    exit 0
fi

run mkdir -p "$prof/extensions"
pack
all_prefs set_pref
install_chrome
if [ "$addons" = 1 ]; then
    all_addons
fi

if [ "$dry" = 1 ]; then
    echo "Dry run: nothing was changed."
    exit 0
fi
echo
echo "Installed into $prof"
echo
echo "Now, in IceCat:"
echo "  1. Start IceCat. For each new add-on it asks whether to"
echo "     enable it; say yes."
echo "  2. In about:addons, for switch, set 'Run in Private"
echo "     Windows' to Allow."
echo "  3. Alt+Shift+S opens the switch popup."
