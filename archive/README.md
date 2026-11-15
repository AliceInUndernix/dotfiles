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
| `tmux/tmux-3.8.tar.gz` | tmux release (if a distro ever lacks it) | https://github.com/tmux/tmux/releases | 3.8 |
| `nvim-plugins/zen-mode.nvim.tar.gz` | prose mode view | https://github.com/folke/zen-mode.nvim | `8564ce6` (2025-10-28) |
| `nvim-plugins/vim-pencil.tar.gz` | prose wrapping | https://github.com/preservim/vim-pencil | `6d70438` (2023-04-03) |
| `nvim-plugins/plover-vim-tutor.tar.gz` | plover-vim help pages | https://github.com/Josiah-tan/plover-vim-tutor | `8015ae4` (2026-07-07) |
| `nvim-plugins/mini.pick.tar.gz` | fuzzy finder | https://github.com/nvim-mini/mini.pick | `f1216d7` (2026-11-07) |
| `nvim-plugins/mini.surround.tar.gz` | surround quotes/brackets | https://github.com/nvim-mini/mini.surround | `5357f00` (2026-10-27) |
| `nvim-plugins/which-key.nvim.tar.gz` | key hints | https://github.com/folke/which-key.nvim | `3aab214` (2025-10-28) |
| `moonlander/optozorax-moonlander.tar.gz` | optozorax's keymap and libraries (`lang_shift`, `combo`, `color`, `arbitrary_keycode`, `moonlander_leds.h`, `qmk.patch`) | https://github.com/optozorax/moonlander | `8b725d0` (2026-07-30) |

Full commits:

- zen-mode.nvim `8564ce6d29ec7554eb9df578efa882d33b3c23a7`
- vim-pencil `6d70438a8886eaf933c38a7a43a61adb0a7815ed`
- plover-vim-tutor `8015ae48fd3cc39ea3682b9094874a688d9f63d9`
- mini.pick `f1216d764d0c2772e88d4e1a0cef3082f5b498e1`
- mini.surround `5357f00aca51a3b43e7775901b1ad9699f8c2678`
- which-key.nvim `3aab2147e74890957785941f0c1ad87d0a44c15a`
- optozorax/moonlander `8b725d0876b16b788f18ab5530ce6440cf979b3d`

The suckless tarballs match the checksums suckless publishes in each
directory's `sha256sums.txt`. The git snapshots were made with
`git archive` at the commit above.

## Using them

- **Neovim plugins:** `./install.sh nvim` unpacks any missing plugin from
  here into `~/.system/share/nvim/site/pack/plugins/opt/`, so Neovim works
  on first start without internet. Delete that directory to download
  fresh copies instead.
- **suckless:** `tar xf suckless/dwm-6.8.tar.gz` gives the pristine
  source, to compare with `../dwm/` or start over from.
- **Moonlander:** the libraries are already copied into
  `moonlander/canary/`. The tarball keeps optozorax's whole keymap; its
  library directories are git submodules and come out empty.

## Not archived

- **ZSA's QMK fork** (firmware25): about 20 MB unpacked (1.8 MB
  compressed) on its own, plus submodules such as ChibiOS that are much
  larger. Left out until decided.
- **Claude Code**, used by the Neovim proofreading key.
