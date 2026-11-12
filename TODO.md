# To do

## Status

- **Finished:** dwm, dmenu, st, slock (keep as it is), tmux, Neovim,
  install.sh (runs as root, no sudo).
- **Later, Alice configures them herself or asks:** mpv, Sioyek,
  Flameshot, wiremix, btop, feh.
- **Not started:** IceCat and Chromium settings.

## Decided, left for later

- **Notification daemon.** Pick one (dunst is the one suckless lists on
  its "rocks" page) and set it up in Habamax colours, started from
  `x/xinitrc`.
- **Fewer hidden files in `~`.** Done for what this repo manages:
  programs, their data, state and caches are in `~/.system`; only
  `~/.config`, `~/.profile`, `~/.kshrc`, `~/.xinitrc` (links) and
  `~/.system` remain. Programs that ignore XDG (browsers' `~/.pki`,
  `~/.mozilla`, ssh, gnupg) still make their own folders.
