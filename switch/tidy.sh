#!/bin/sh
# Remove what IceCat leaves on disk after "switch" has wiped a site,
# files whose names or contents alone show which sites were visited:
#   - empty per-site storage directories (storage/*/http*);
#   - the HSTS list (SiteSecurityServiceState) and the list of other
#     addresses sites offered (AlternateServices);
#   - the session (sessionstore.jsonlz4, sessionstore-backups);
#   - favicons.sqlite, and the page thumbnails and cache directories.
# Directories that still hold data (kept sites) are left alone; so are
# cookies, history and bookmarks (switch and IceCat clear those). The
# icecat command of the dotfiles (switch/icecat) runs this every time
# IceCat quits. Run as your ordinary user, with IceCat closed.
#
# Usage: ./tidy.sh [-n] [-q] [-r] [profile_directory]
#   -n  dry run: list what would be removed, change nothing
#   -q  quiet: say nothing, and do nothing while IceCat still runs
#   -r  run IceCat first, and tidy when it exits
# Set ICECAT_ROOT to use another IceCat directory.

set -e

root="${ICECAT_ROOT:-$HOME/.mozilla/icecat}"
dry=0
quiet=0
run=0

die()
{
    echo "tidy.sh: $*" >&2
    exit 1
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

while getopts nqr opt; do
    case "$opt" in
    n) dry=1 ;;
    q) quiet=1 ;;
    r) run=1 ;;
    *) die "usage: tidy.sh [-n] [-q] [-r] [profile_directory]" ;;
    esac
done
shift $((OPTIND - 1))

[ "$(id -u)" != 0 ] || die "run this as your ordinary user, not root"
if [ $# -gt 0 ]; then
    prof="$1"
else
    prof=$(default_profile)
fi
[ -d "$prof" ] || die "no profile directory $prof"

if [ "$run" = 1 ]; then
    icecat || true
    sleep 2
fi
# IceCat holds a "lock" link to "address:+pid" while it uses a profile
lock=$(readlink "$prof/lock" 2> /dev/null) || lock=""
pid=${lock##*+}
case "$pid" in
""|*[!0-9]*) ;;
*)
    if kill -0 "$pid" 2> /dev/null; then
        [ "$quiet" = 0 ] || exit 0
        die "IceCat is running; close it first"
    fi
    ;;
esac

# rm_path <path>: remove a file or directory, or say it would
rm_path()
{
    [ -e "$1" ] || [ -L "$1" ] || return 0
    if [ "$dry" = 1 ]; then
        echo "would remove $1"
    else
        rm -rf "$1"
    fi
}

n=0
for d in "$prof"/storage/*/http* "$prof"/storage/to-be-removed/*
do
    [ -d "$d" ] || continue
    # Anything but the metadata file means the site still has data
    if [ -n "$(find "$d" -type f ! -name '.metadata*' | head -n 1)" ]
    then
        continue
    fi
    if [ "$dry" = 1 ]; then
        echo "would remove ${d#"$prof"/}"
    else
        rm -r "$d"
    fi
    n=$((n + 1))
done
for f in "$prof"/SiteSecurityServiceState.* \
    "$prof"/AlternateServices.* \
    "$prof"/sessionstore.jsonlz4 "$prof"/sessionstore-backups \
    "$prof"/favicons.sqlite "$prof"/favicons.sqlite-wal \
    "$prof"/favicons.sqlite-shm
do
    rm_path "$f"
done
# the profile's cache directory: ~/.cache/mozilla/icecat/<profile>
name=${prof##*/}
for c in "${XDG_CACHE_HOME:-$HOME/.cache}/mozilla/icecat/$name" \
    "$HOME/.cache/mozilla/icecat/$name"
do
    rm_path "$c/thumbnails"
    rm_path "$c/cache2"
done
[ "$quiet" = 0 ] || exit 0
if [ "$dry" = 1 ]; then
    echo "Dry run: $n directories would be removed."
else
    echo "Removed $n empty site directories and the files that name sites."
fi
