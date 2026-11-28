#!/bin/sh
# Build the "canary" layout for the ZSA Moonlander rev B and flash it.
# Run it as yourself, from anywhere; nothing needs root but flashing,
# and only when your user can't reach the keyboard (no udev rule).
#
#   ./build.sh          set up QMK (once), build, then flash
#   ./build.sh flash    only flash the .bin built last time (root is fine)
#   MOONLANDER_FLASH=no ./build.sh    build without flashing
#
# What it puts where (all in your home directory):
#   $QMK_DIR/zsa_qmk             ZSA's QMK source (branch firmware25),
#                                with qmk.patch applied
#   $QMK_DIR/venv                Python environment for the qmk command
#   $QMK_DIR/arm-gnu-toolchain   Arm GNU Toolchain (always this one)
#   ./zsa_moonlander_revb_canary.bin   the firmware, next to this script
# QMK_DIR is ${XDG_DATA_HOME:-~/.local/share}/qmk unless you set it.
# LINK_DIR=<dir>: also link qmk and arm-none-eabi-* into <dir> (a bin
# directory in your PATH), to run them yourself.
#
# It needs git, curl, python3 (with venv), make, tar with xz, and
# dfu-util for flashing (dfu-suffix from it adds the DFU trailer).
# POSIX sh.

set -eu

say()  { printf '==> %s\n' "$*"; }
warn() { printf '!!  %s\n' "$*" >&2; }
die()  { warn "$*"; exit 1; }

ML_DIR=$(cd "$(dirname "$0")" && pwd)
QMK_DIR=${QMK_DIR:-${XDG_DATA_HOME:-$HOME/.local/share}/qmk}
QMK_BRANCH=firmware25
# Rev B (shipped since December 2025) has its own bootloader
# ("Moonlander Bootloader", USB 3297:2003) that starts the firmware at
# 0x08002000; rev A's is ST's (0483:df11, at 0x08000000). One
# revision's firmware doesn't run on the other.
QMK_TARGET=zsa/moonlander/revb:canary
ML_BIN=zsa_moonlander_revb_canary.bin
ML_DFU_ID=3297:2003
ML_DFU_ADDR=0x08002000
# Gentoo's arm-none-eabi-gcc 16 built firmware that froze the keyboard
# after a few key presses (2026-11-11); this one builds it fine.
ARM_VERSION=14.2.rel1

# flash: dfu-util, waiting up to 2 minutes for the keyboard to be in
# flashing mode. If it never shows up, the .bin is there for Keymapp.
flash() {
    [ -f "$ML_DIR/$ML_BIN" ] || die "no $ML_BIN yet: run ./build.sh first"
    if ! command -v dfu-util >/dev/null 2>&1; then
        warn "dfu-util not found: flash $ML_DIR/$ML_BIN with Keymapp"
        return 0
    fi
    say "flash   put the Moonlander in flashing mode now:"
    say "        tap the far-left lower key (backlight layer), then BOOT"
    say "        (the key next to Ctrl), or press the reset pin hole"
    i=0
    until dfu-util -l 2>/dev/null | grep -q "$ML_DFU_ID"; do
        i=$((i + 1))
        if [ "$i" -gt 120 ]; then
            warn "no keyboard in flashing mode after 2 minutes: flash"
            warn "$ML_DIR/$ML_BIN with Keymapp, or run ./build.sh flash"
            warn "(as root if your user can't reach USB devices)"
            return 0
        fi
        sleep 1
    done
    if dfu-util -a 0 -d "$ML_DFU_ID" -s "$ML_DFU_ADDR:leave" \
        -D "$ML_DIR/$ML_BIN"
    then
        say "flashed the Moonlander"
    else
        warn "dfu-util reported an error; if the keyboard doesn't restart,"
        warn "flash $ML_DIR/$ML_BIN with Keymapp"
    fi
}

case ${1:-} in
flash) flash; exit 0 ;;
"") ;;
*) die "usage: ./build.sh [flash]" ;;
esac

[ "$(id -u)" -ne 0 ] ||
    die "build as yourself, not root (only ./build.sh flash may be root)"
for t in git curl python3 make tar; do
    command -v $t >/dev/null 2>&1 || die "$t not found"
done
fw=$QMK_DIR/zsa_qmk
mkdir -p "$QMK_DIR"

# 1. ZSA's QMK source, only the latest commit to keep it small.
if [ ! -d "$fw/.git" ]; then
    say "clone   zsa/qmk_firmware ($QMK_BRANCH) -> $fw"
    git clone --depth 1 --branch "$QMK_BRANCH" --recurse-submodules \
        --shallow-submodules https://github.com/zsa/qmk_firmware "$fw"
fi

# 2. qmk.patch: key events can carry any keycode (arbitrary_keycode),
# the RGB code ignores those, the audio timer is set from config.h.
if git -C "$fw" apply --reverse --check "$ML_DIR/qmk.patch" 2>/dev/null
then
    say "ok      qmk.patch already applied"
else
    git -C "$fw" apply "$ML_DIR/qmk.patch"
    say "patch   qmk.patch -> $fw"
fi

# 3. The qmk command in its own Python environment. A venv can't be
# moved (its scripts name their old path), so one that no longer runs
# is made again.
if [ -d "$QMK_DIR/venv" ] &&
    ! "$QMK_DIR/venv/bin/qmk" --version >/dev/null 2>&1
then
    rm -rf "$QMK_DIR/venv"
fi
if [ ! -x "$QMK_DIR/venv/bin/qmk" ]; then
    say "python  qmk -> $QMK_DIR/venv"
    python3 -m venv "$QMK_DIR/venv"
    PIP_NO_CACHE_DIR=1 "$QMK_DIR/venv/bin/python" -m pip install --quiet \
        qmk -r "$fw/requirements.txt"
fi
"$QMK_DIR/venv/bin/qmk" config user.qmk_home="$fw" >/dev/null 2>&1 ||
    warn "could not set qmk's user.qmk_home to $fw"

# 4. The ARM compiler: always this pinned Arm GNU Toolchain, even when
# the system has one (see ARM_VERSION above).
arm=$QMK_DIR/arm-gnu-toolchain
if [ ! -x "$arm/bin/arm-none-eabi-gcc" ]; then
    name=arm-gnu-toolchain-$ARM_VERSION-$(uname -m)-arm-none-eabi
    url=https://developer.arm.com/-/media/Files/downloads/gnu/$ARM_VERSION
    url=$url/binrel/$name.tar.xz
    say "fetch   $name"
    rm -rf "$arm" "$arm.tmp"
    mkdir -p "$arm.tmp"
    curl -fL "$url" | tar -xJ -C "$arm.tmp" --strip-components=1
    mv "$arm.tmp" "$arm"
fi

# Links for running qmk and the compiler yourself (LINK_DIR).
if [ -n "${LINK_DIR:-}" ]; then
    mkdir -p "$LINK_DIR"
    for t in "$QMK_DIR/venv/bin/qmk" "$arm"/bin/arm-none-eabi-*; do
        ln -sfn "$t" "$LINK_DIR/${t##*/}"
    done
    say "link    qmk, arm-none-eabi-* -> $LINK_DIR"
fi

# 4b. ZSA's rev B code moves the stack pointer with an asm "g" operand;
# gcc 16 then picks a memory operand that msr can't take ("Thumb
# encoding does not support an immediate here"). "r" (a register) is
# what msr needs, on every gcc.
ml=$fw/keyboards/zsa/moonlander/moonlander.c
if grep -q '"msr msp, %0" ::"g"' "$ml"; then
    sed 's/"msr msp, %0" ::"g"/"msr msp, %0" ::"r"/' "$ml" > "$ml.tmp"
    mv "$ml.tmp" "$ml"
    say "fix     msr operand in $ml (for gcc 16)"
fi

# 5. The keymap: a fresh copy of canary/.
km=$fw/keyboards/zsa/moonlander/keymaps/canary
rm -rf "$km"
cp -R "$ML_DIR/canary" "$km"
say "copy    canary -> $km"

# 6. Build. Without dfu-suffix the .bin just has no DFU trailer.
nosuffix=
if ! command -v dfu-suffix >/dev/null 2>&1; then
    warn "dfu-suffix not found (dfu-util): building without the DFU trailer"
    nosuffix=DFU_SUFFIX=true
fi
say "build   $QMK_TARGET"
(
    cd "$fw"
    PATH=$QMK_DIR/venv/bin:$arm/bin:$PATH
    export PATH
    make "$QMK_TARGET" $nosuffix
)
cp "$fw/$ML_BIN" "$ML_DIR/$ML_BIN"
say "built   $ML_DIR/$ML_BIN"

# 7. Flash.
[ "${MOONLANDER_FLASH:-yes}" = no ] || flash
