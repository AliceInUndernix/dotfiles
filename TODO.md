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
- **Moonlander lock and suspend keys.** Add keys sending Super+Shift+L
  (dwm's lock) and Super+Shift+S (dwm's suspend) with the next batch of
  layout changes; see `moonlander/HANDOFF.md`.
- **Moonlander dwm layer to match dwm.** The layer sends Super+; for
  dmenu, Super+K/N/E/I for focus/master and Super+` for previous tag, but
  dwm uses Mod+p, Mod+h/j/k/l and Mod+Tab, and Mod+` is now the
  scratchpad. Change the keyboard (not dwm) once dwm's keys are settled.
