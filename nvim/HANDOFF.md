# Handoff: Neovim config for steno prose writing

Written 2026-11-11 so a new Claude project can continue this work from the
files alone. Read this first, then `README.md` (user guide), then the code.
The comments in `nvim/init.lua` and `nvim/lua/typetrack.lua` describe the
current behaviour section by section.

## Files

In the dotfiles repo (github.com/AliceInUndernix/dotfiles); install.sh
links the whole `nvim/` folder to `~/.config/nvim`.

```
nvim/
  HANDOFF.md            this file
  README.md             user guide: install, keys, every feature
  init.lua              the config
  lua/typetrack.lua     book typing checker (ghost/check modes)
```

## The user and their setup

- Gentoo Linux. Neovim 0.11.7 from the stable tree. Upgrading to 0.12
  was attempted and needs `~amd64` keywords for both
  `app-editors/neovim` and `dev-lua/luv` in
  `/etc/portage/package.accept_keywords/neovim`; whether they finished
  is unknown. The config must keep working on 0.11 (no `vim.pack`).
- Writes fiction with stenography (Plover). Spelling comes from steno,
  so spell checks are off by default; grammar and misstroke checks are
  wanted.
- Terminal: st 0.9.3 (patched, see `st/UPSTREAM`), often with tmux,
  under dwm on X11. Changes go to the dotfiles repo; `~/.config/nvim` is
  a symlink to it, so nothing needs copying.
- Uses Sioyek as EPUB/PDF reader, 2.0.0 at `~/.system/bin/sioyek`.
- Writes short, sometimes cut-off messages (often dictated or stenoed);
  ask what they meant when a message stops mid-sentence.
- Prefers plain explanations and a config that stays close to default.

## What was asked for, and how it was done

| Request | Implementation | Status |
|---|---|---|
| Minimal config, colour column 80, hybrid line numbers | options at top of init.lua | user ran it |
| Dark theme "Habamax" | built-in `habamax`; Gruvbox dropped | user ran it |
| Prose mode, wrap at 80, distraction-free | zen-mode.nvim + vim-pencil (hard wrap), `Space p` | user ran it |
| Plugins on 0.11 | git clone into pack/plugins/opt + packadd; vim.pack needs 0.12 | user ran it |
| Remember cursor, folds, undo between sessions | undofile/viewdir/shada in stdpath("state"), mkview/loadview autocmds | not confirmed |
| Single Enter must not be pulled back into the paragraph | `pencil#autoformat = 0`; `gqip` to re-wrap | fixed after user report, not confirmed |
| Arrows move by screen line in prose mode only | buffer-local gj/gk maps added on_open, removed on_close | not confirmed |
| Grammar check (not spelling) | Harper LSP via vim.lsp.config, SpellCheck linter off | not confirmed |
| Claude proofreading with the signed-in account, current file only | `Space g`: `claude -p` with no tools, no MCP, empty cwd, API-key vars unset, Sonnet, low effort | user ran it; it over-marked fine lines |
| Show exact words, explain, offer a fix, work in prose mode | diagnostics with col ranges, popup on CursorHold, `Space f` applies | prompt tightened after over-marking, not confirmed |
| Spell check toggle | `Space s` | not confirmed |
| plover-vim | only plover-vim-tutor help pages installed; user is NOT using plover-vim yet (doesn't want to learn Emily's modifiers) but wants the line kept | `:help plover-vim-tutorial` opened in a test |
| Em dash etc. from Plover arrived as "2014" | insert-mode `<C-S-u>` expr map reads hex digits and inserts the character; st sends Ctrl+Shift+U as CSI u, tmux has extended-keys on | key tested reaching Neovim in st and st+tmux; real Plover symbol not confirmed |
| System clipboard | `clipboard=unnamedplus`; xclip and xsel installed by `install.sh --deps` | not confirmed |
| Mark typed words green/red in Sioyek | lua/typetrack.lua, `:TypeTrack`, `Space t` | Neovim side tested; never run against Sioyek; Sioyek part now off (crashed) |
| Skipped words were never flagged; ghost mode (grey book text to type over, wrong letters red) and check mode (no ghosts); letter-by-letter and word-by-word; skip front matter (2026-11-14) | typetrack.lua rewritten around align() + char_diff(); `:TypeGhost`/`Space T`, `:TypeTrack`/`Space t`, `:TypeChapter` (TOC picker) | tested headless on Neovim 0.9.5 only; inline ghosts not seen in a real terminal. Her request ended at "skip chapters if": asked what she meant |

## Open issues (pick these up first)

1. **Ghost mode not yet seen in a real terminal** (inline virt text, Neovim 0.10+). Ask how it looks and whether skipped words are flagged right.
2. **Sioyek tracker has never run against Sioyek** (now off by default). It needs the
   development Sioyek (`select_current_search_match` command), not 2.0.0
   or sioyek3-alpha0. `install.sh --deps` now builds it on Gentoo
   (GURU's `sioyek-9999`, newest commit each run, with `--update --deep`
   for the earlier Qt slot conflict), and `install.sh sioyek` moves the
   hand-installed `~/.system/bin/sioyek` (2.0.0) aside after backing up
   `~/.system/share/sioyek`. Not yet run on her machine.
3. **Plover unicode fix: terminal side done.** st and tmux now pass
   Ctrl+Shift+U to Neovim (tested). Not yet confirmed with Plover itself;
   if digits still appear, check that st and tmux were rebuilt/reloaded.
4. **Claude proofreading accuracy.** After the prompt was tightened, it
   has not been re-tested. `:ClaudeLog` shows the raw answer; ask the
   user to paste it if marks look wrong.
5. **init.lua itself has not been run by Claude** (only the Ctrl+Shift+U
   key test, on an older Neovim). The user's reports confirm the rest.
   Ask for `:messages` output if anything errors on start.

## Rules for editing

- Keep it minimal and close to Neovim defaults; no plugin manager.
- Keep everything that must work without plugins above the plugin
  section in init.lua: the file `return`s early if zen-mode or pencil
  fail to download.
- Leader is Space. Taken: w, p, s, g, f, G, t, T, Space, /, b, h.
- nvim-treesitter is on the frozen `master` branch because the user is on
  Neovim 0.11; `main` needs 0.12. Highlighting only: the user does not
  want LSP completion or error checking for code (asked 2026-11-12).
  Declined for now: gitsigns, vim-fugitive, language servers.
  vim-tmux-navigator was tried and removed at the user's request (2026-11-12).
- Keep the plover-vim-tutor line.
- Keep `claude` locked down: no tools, `--strict-mcp-config`, empty cwd,
  `ANTHROPIC_API_KEY` / `ANTHROPIC_AUTH_TOKEN` removed, only the current
  buffer on stdin. The user asked for exactly this.
- Say plainly what was and wasn't tested. Commit and push changes to
  main in the dotfiles repo.

## Ideas offered but not taken

- A starter Plover dictionary of ~20 Vim command strokes (Esc, save,
  undo, ciw, gq...). Offered by the steno research, not requested yet.
- Other steno-in-Vim approaches researched: Emily's modifiers,
  plover-dict-commands (`SOLO_DICT`), plover-modal-dictionary, and toggling
  Plover off (`PHRO*F`) for keyboard editing.
