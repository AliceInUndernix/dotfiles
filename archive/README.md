# archive

Copies of everything this setup downloads from somewhere else, so it can
be rebuilt even if an upstream site or repository disappears. Taken
2026-11-11. Check the files with `sha256sum -c SHA256SUMS` in this directory.

These are untouched upstream copies. The versions actually used (and
edited) live in the app directories at the top of the repo.

| File | What | Source | Version |
|---|---|---|---|
| `suckless/dwm-6.8.tar.gz` | dwm release | https://dl.suckless.org/dwm/ | 6.8 |
| `suckless/st-0.9.3.tar.gz` | st release | https://dl.suckless.org/st/ | 0.9.3 |
| `suckless/dmenu-5.4.tar.gz` | dmenu release | https://dl.suckless.org/tools/ | 5.4 |
| `suckless/slock-1.7.tar.gz` | slock release | https://dl.suckless.org/tools/ | 1.7 |
| `monocypher/monocypher-4.0.2.tar.gz` | the crypto library `seal/crypto.c` is taken from | https://github.com/LoupVaillant/Monocypher | 4.0.2 (`0d85f98`, 2023-09-02) |
| `fonts/NerdFontsSymbolsOnly-3.5.1.tar.xz` | Symbols Nerd Font, st's icon font (`./install.sh st` unpacks it) | https://github.com/ryanoasis/nerd-fonts/releases | 3.5.1 |
| `tmux/tmux-3.8.tar.gz` | tmux release (if a distro ever lacks it) | https://github.com/tmux/tmux/releases | 3.8 |
| `nvim-plugins/zen-mode.nvim.tar.gz` | prose mode view | https://github.com/folke/zen-mode.nvim | `8564ce6` (2025-10-28) |
| `nvim-plugins/vim-pencil.tar.gz` | prose wrapping | https://github.com/preservim/vim-pencil | `6d70438` (2023-04-03) |
| `nvim-plugins/mini.pick.tar.gz` | fuzzy finder | https://github.com/nvim-mini/mini.pick | `f1216d7` (2026-11-07) |
| `nvim-plugins/mini.surround.tar.gz` | surround quotes/brackets | https://github.com/nvim-mini/mini.surround | `5357f00` (2026-10-27) |
| `nvim-plugins/which-key.nvim.tar.gz` | key hints | https://github.com/folke/which-key.nvim | `3aab214` (2025-10-28) |

Full commits:

- zen-mode.nvim `8564ce6d29ec7554eb9df578efa882d33b3c23a7`
- vim-pencil `6d70438a8886eaf933c38a7a43a61adb0a7815ed`
- mini.pick `f1216d764d0c2772e88d4e1a0cef3082f5b498e1`
- mini.surround `5357f00aca51a3b43e7775901b1ad9699f8c2678`
- which-key.nvim `3aab2147e74890957785941f0c1ad87d0a44c15a`
- Monocypher 4.0.2 `0d85f98c9d9b0227e42cf795cb527dff372b40a4`

The suckless tarballs match the checksums suckless publishes in each
directory's `sha256sums.txt`, and the Nerd Font the release's
`SHA-256.txt`. The git snapshots were made with
`git archive` at the commit above.

## Using them

- **Neovim plugins:** `./install.sh nvim` unpacks any missing plugin from
  here into `~/.system/share/nvim/site/pack/plugins/opt/`, so Neovim works
  on first start without internet (Neovim itself downloads nothing).
- **st's font:** `./install.sh st` unpacks it from here; nothing is
  downloaded.
- **suckless:** `tar xf suckless/dwm-6.8.tar.gz` gives the pristine
  source, to compare with `../dwm/` or start over from.

## Not archived

- **Moonlander:** optozorax's keymap and libraries, and the notes on
  ZSA's QMK fork, are in the moonlander repo
  (https://github.com/AliceInUndernix/moonlander, its `archive/`).
  Its build.sh still downloads ZSA's QMK source, the qmk Python
  package and the Arm GNU Toolchain 14.2.rel1 on the first build.
- **The steno dictionaries, the Moonlander layout and kspkan** are
  your own repositories (dotterel, moonlander, kspkan), cloned by
  install.sh, not copies of someone else's.
- **Packages** come from Gentoo with emerge (its own distfiles).
- **Data fetched while a program runs** can't be a fixed copy:
  kspkan's CKAN mod list, the VPN's server list and certificates
  (net/vpn), and the mods themselves.
