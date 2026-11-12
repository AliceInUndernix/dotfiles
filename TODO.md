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
- **Fewer hidden files in `~`.** Alice wants a clean, transparent home
  with no folders she doesn't know. Everything installed already lives in
  `~/.system` (commands in `~/.system/bin`, first in PATH; `~/.local/bin`
  is retired). Later: move what can be moved out of `~` (e.g. ksh files,
  `~/.local/share` and `~/.local/state` via XDG variables into
  `~/.system`) and list what must stay.
