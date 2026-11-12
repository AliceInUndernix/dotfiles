# Minimal Neovim config

Two files: `nvim/init.lua` (the config) and `nvim/lua/typetrack.lua`
(optional, the Sioyek typing tracker). It keeps Neovim close to default
and adds a colour column at 80, line numbers, a dark theme and a prose
mode for writing with stenography.

`HANDOFF.md` next to this file records the history, decisions and open
issues, for anyone (or any Claude) picking the work up.

## Install

In the dotfiles repo, `./install.sh nvim` links this folder to
`~/.config/nvim`; the steps below are the manual way. Paths below that
mention `nvim-config/nvim` now mean this `nvim/` folder.

1. You need Neovim **0.11 or newer** (`nvim --version`) and `git`.
2. Back up any old config: `mv ~/.config/nvim ~/.config/nvim.bak`
3. Copy the files from Downloads:

       mkdir -p ~/.config/nvim/lua
       cp ~/Downloads/init.lua ~/.config/nvim/init.lua
       cp ~/Downloads/typetrack.lua ~/.config/nvim/lua/typetrack.lua

   From the archive instead: unpack it and copy its `nvim` folder to
   `~/.config/nvim` (`cp -r nvim-config/nvim/. ~/.config/nvim/`).
4. Start `nvim` while online. On first start it downloads its 7 plugins (`./install.sh nvim`
   unpacks them from `archive/` instead) and builds the Tree-sitter
   parsers for common languages in the background (needs gcc).

## Keys

| Key | What it does |
|---|---|
| `Space p` / `:Prose` | Prose mode on and off |
| `Space s` | Spell check on and off |
| `Space g` | Proofread the file with Claude |
| `Space f` | Apply Claude's fix under the cursor |
| `Space G` | Clear Claude's marks (`:ClaudeLog` shows its last answer) |
| `Space t` | Sioyek typing tracker on and off |
| `Space w` | Save |
| `Space Space` | Open a file by typing part of its name |
| `Space /` | Search text in all files (needs `rg` or a git folder) |
| `Space b` | Pick one of the open files |
| `Space h` | Search Neovim's help |
| `saiw"` / `sd"` / `sr"'` | Add / delete / replace quotes or brackets around |
| `Space` and wait | A box lists the keys that can follow (which-key) |
| `Esc` | Clear search highlight |
| `]d` / `[d` | Next / previous grammar or Claude mark |

## Two modes

**Code mode** is the normal state: relative line numbers with the real
number on your current line, a vertical guide at column 80, no wrapping.

**Prose mode** (`Space p` or `:Prose`, same key to leave): the text sits
in a centred 80-column view, numbers, the guide and the status bar
disappear, lines break automatically at 80. A single Enter starts a new
line and stays there (book-style paragraphs); after editing the middle
of a paragraph, `gqip` re-wraps it. If a paragraph is one long line,
`j` / `k` and the arrows move one screen line at a time; with a count
(`5j`) they move real lines. Code mode keeps normal movement.

Markdown and `.txt` files wrap at 80 even outside prose mode. Spell
check is off by default because steno handles spelling; `Space s` turns
it on.

Useful prose keys: `gqip` re-wraps the current paragraph, `]s` / `[s` jump
between misspellings, `z=` suggests a fix, `zg` adds a word to your
dictionary.

## Symbols from steno (— – “ ” …)

On Linux, Plover types symbols as Ctrl+Shift+U, a hex code, then Space.
Most terminals ignore that shortcut, so you got `2014` instead of `—`.
The config catches the sequence in insert mode and puts the real symbol
in. st (`st/config.h`) sends Ctrl+Shift+U as its own key and tmux passes
it on (`extended-keys`), so this works in st and in tmux inside st. By
hand: `Ctrl-k - M` types —, `Ctrl-k - N` types –.

## Plugins

| Plugin | Why |
|---|---|
| zen-mode.nvim | The distraction-free centred view |
| vim-pencil | Proper hard wrapping at 80 for prose |
| plover-vim-tutor | Help pages for plover-vim (see below) |
| nvim-treesitter (`master`) | Syntax highlighting for many languages (highlighting only) |
| mini.pick | Fuzzy finder for files, text, buffers, help |
| mini.surround | Add, delete, replace quotes and brackets |
| which-key.nvim | Shows what keys can follow after a pause |

To update them, delete `~/.local/share/nvim/site/pack/plugins/` and
restart Neovim; they download again. Then run `:TSUpdate` once.

## Tabs and whitespace

The Tab key types 4 spaces, and Backspace removes 4 at once, so they
feel like one tab. Real tab characters in existing files are left alone
(dwm, st and dmenu keep theirs) and show as a dim `→` filling 4 columns.
Trailing spaces show as dim `·` (not while you type), no-break spaces
as `␣`, and `›` / `‹` mark lines that go past the screen edge.
`:s/old/new` shows its changes live while you type.

## Grammar check (optional)

The config uses Harper, an offline grammar checker, when it's installed.
Install it once with Rust's cargo (`sudo emerge dev-lang/rust-bin` if
you don't have cargo):

    cargo install harper-ls --locked

Make sure `~/.cargo/bin` is in your PATH. In Markdown, text and git
commit files, problems are underlined; rest the cursor on one and a
pop-up explains it. `]d` / `[d` jump between them, `Ctrl-w d` also
shows the message, and `gra` offers a fix. Harper's spell check is off.

## Proofread with Claude (optional)

`Space g` sends the file you are in to Claude (Sonnet, low effort) and
underlines the exact words it thinks are wrong. Rest the cursor on them
to see why and the suggested fix; `Space f` applies it. `]d` / `[d` jump
between marks, `Space G` clears them, `:ClaudeLog` shows Claude's last
raw answer (saved on your computer only).
Nothing in your file changes unless you press `Space f`.

Setup, once: install Claude Code (https://claude.com/claude-code), run
`claude` in a terminal and sign in with your Claude account.

Privacy: only the text of the current file is sent, piped in directly.
Claude runs with every tool switched off and no connectors, from an empty
temporary folder, so it cannot open other files or run commands. API-key
variables are removed, so it always uses your signed-in account. The text
does go to Claude online and counts toward your usage.

## Terminal theme

Habamax is built into Neovim (no plugin). st, dwm, dmenu, slock and tmux
use the same colours (background `#1c1c1c`, text `#c7c7c7`, greys for
borders, toned-down Habamax accents), and st has Habamax's full
16-colour palette.

## Clipboard

`y` and `p` use the system clipboard (`clipboard=unnamedplus`), so text
moves between Neovim and the browser. Neovim needs xclip or xsel for
that; `./install.sh --deps` installs both.

## What Neovim remembers between sessions

Everything is kept in `~/.local/state/nvim/`, out of your project folders:

- `undo/`: full undo history per file, so `u` works even after a restart.
- `view/`: your folds and cursor position per file.
- `shada/`: last cursor position, marks, registers and command history.

Close a file, reopen it, and the cursor, folds and undo history are where
you left them. Line breaks from wrapping at 80 are real line breaks saved
in the file itself, so they are always in the same place. Swap files are off.

## Steno Vim commands (plover-vim)

The config installs plover-vim-tutor, which is only help pages: the
lessons (`:help plover-vim-tutorial`) and a cheatsheet
(`:help plover-vim-cheatsheet`). You're not using plover-vim for now;
the line stays so it's ready later. The steno side lives in Plover:

1. Close Plover, then install the plugins, e.g. with the AppImage:
   `./plover-*.AppImage -s plover_plugins install plover_vim plover-python-dictionary`
2. Get the tutor dictionary:
   `curl -o ~/.config/plover/vim.py https://raw.githubusercontent.com/Josiah-tan/plover-vim/main/templates/tutor/escaped_customised.py`
3. Start Plover, add `vim.py` with **+** under Dictionaries, and tick
   **Start attached** in Configure → Output.
4. In Neovim run `:set scrolloff=9`, then `:help plover-vim-tutorial`.

The tutor expects Emily's modifiers before its modifier lessons.

## Type a book along in Sioyek (optional)

Open a book in Sioyek, type it out in a text file in Neovim, and Sioyek
marks each word of the book: **green** when you typed it right, **red**
when you typed it wrong or skipped it. Wrong words are also underlined in
your text file. It needs `lua/typetrack.lua` (see Install).

You need:

- A **development** Sioyek (Gentoo GURU's `app-text/sioyek-9999`).
  Release 2.0.0 cannot do it. Check with `sioyek --version`.
- `unzip` for EPUB books, or `pdftotext` (`app-text/poppler`) for PDFs.

Installing sioyek-9999 on Gentoo:

    sudo emerge --ask app-eselect/eselect-repository
    sudo eselect repository enable guru
    sudo emaint sync -r guru
    echo "app-text/sioyek **" | sudo tee /etc/portage/package.accept_keywords/sioyek
    sudo emerge --ask --update --deep --changed-use @world =app-text/sioyek-9999

(The `@world` part avoids the Qt 6.11.2 / 6.11.3 slot conflict.) Back
up `~/.local/share/sioyek` first, and move an old `~/.system/bin/sioyek`
out of the way so the new one starts.

Use: save your text file, then `:TypeTrack ~/books/book.epub` (Tab
completes the path). After that `Space t` starts and stops it, and it
remembers which book goes with the file and how far it has marked, so
it carries on next time. Text already in the file is left alone the
first time unless you use `:TypeTrack!`. `:TypeTrackStop` stops it.

How it judges: it finds where your first words are in the book and
then follows along word by word. Capitals and punctuation count; curly
and straight quotes, and `--` and `—`, count as the same. It waits two
words (or a 1.5 second pause) before judging, so a quick steno
correction is not marked red.

Colours: Sioyek's highlight types `g` and `r`. To change them, add to
`~/.config/sioyek/prefs_user.config`, for example:

    highlight_color_g 0.30 0.80 0.35
    highlight_color_r 0.95 0.25 0.25

Good to know:

- This has not been tried with a real Sioyek yet, so the first run may
  need fixing.
- Keep the Sioyek window open (not minimised). It scrolls along with
  your typing, and it doesn't take the keyboard away from Neovim.
- Marks are real Sioyek highlights and stay in the book. A word fixed
  after it was marked keeps its old colour in Sioyek (the underline in
  Neovim does update). Delete highlights in Sioyek as usual.
- Changing the EPUB font size in Sioyek moves the text, so old marks
  may no longer sit on the right words.
