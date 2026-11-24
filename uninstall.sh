#!/bin/sh
# Take out what install.sh put in: the same as ./install.sh uninstall.
#   ./uninstall.sh                 everything (not the browsers)
#   ./uninstall.sh steno media     only those parts and groups
#   ./uninstall.sh --purge         and steno's data too
#   su -c './uninstall.sh net'     root's parts need root
# See install/uninstall.sh for what it does.
exec "$(dirname "$0")/install.sh" uninstall "$@"
