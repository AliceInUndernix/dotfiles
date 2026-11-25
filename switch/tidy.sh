#!/bin/sh
# Remove what IceCat leaves on disk after "switch" has wiped a site:
# empty per-site storage directories, whose names alone show which
# sites were visited. Directories that still hold data (kept sites)
# are left alone. Run as your ordinary user, with IceCat closed.
#
# Usage: ./tidy.sh [-n] [-r] [profile_directory]
#   -n  dry run: list what would be removed, change nothing
#   -r  run IceCat first, and tidy when it exits
# Set ICECAT_ROOT to use another IceCat directory.

set -e

root="${ICECAT_ROOT:-$HOME/.mozilla/icecat}"
dry=0
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

while getopts nr opt; do
    case "$opt" in
    n) dry=1 ;;
    r) run=1 ;;
    *) die "usage: tidy.sh [-n] [-r] [profile_directory]" ;;
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
if pgrep -x icecat > /dev/null; then
    die "IceCat is running; close it first"
fi

n=0
for d in "$prof"/storage/default/http* "$prof"/storage/to-be-removed/*
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
if [ "$dry" = 1 ]; then
    echo "Dry run: $n directories would be removed."
else
    echo "Removed $n empty site directories."
fi
