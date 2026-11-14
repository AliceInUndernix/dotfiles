# Minimal Neovim config

Two files: `nvim/init.lua` (the config) and `nvim/lua/typetrack.lua`
(optional, the book typing checker). It keeps Neovim close to default
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
| `Space t` | Book typing check on and off |
| `Space T` | Book typing with ghost text on and off |
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

To update them, delete `~/.system/share/nvim/site/pack/plugins/` and
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

Everything is kept in `~/.system/state/nvim/`, out of your project folders:

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

## Type a book out (optional)

Read a book in Sioyek and type it out in a text file in Neovim. Neovim
checks every word against the book as you type. It needs
`lua/typetrack.lua` (see Install) and `unzip` for EPUB books, or
`pdftotext` (`app-text/poppler`) for PDFs. Sioyek itself is not needed
for the checking.

Two modes:

- **Ghost mode** (`:TypeGhost ~/books/book.epub`, then `Space T`): the
  book's next words are shown in grey after your cursor, with a few more
  lines below. They are not in your file and cannot be edited; you type
  over them. Paragraph breaks in the book show as a blank line.
- **Check mode** (`:TypeTrack ~/books/book.epub`, then `Space t`): the
  same checks, no grey text.

What you see in both:

- A wrong letter turns **red** (underlined) as soon as you type it.
- A word that is not in the book turns red.
- A letter you left out appears crossed out in red where it belongs.
- A book word you **skipped** appears crossed out in red, in front of
  the word you typed after it. A skipped word shows up once you have
  typed the word after it (until then it looks like a typo).

Choosing where to start: the first time, a list of the book's chapters
(from its table of contents) opens, so you can pick "Chapter 1" and skip
the cover, copyright page and foreword. `:TypeChapter` opens the list
again. For a PDF the list has one entry per page. In check mode the list
also offers "Find the place from what I type", which looks for your
first five words in the book. If the file already has text, it lines up
from where that text is in the chosen chapter; otherwise what you type
next starts at the chapter.

The book, the mode and the start are remembered per text file, so
`Space t` / `Space T` carry on next time. `:TypeTrackStop` stops it.

How it judges: capitals and punctuation count; curly and straight
quotes, `...` and `…`, and `--` and `—` count as the same. To check
letters only, add `require("typetrack").opts.strict = false` to
init.lua. The ghost lines wrap at 'textwidth' (80 in prose files).

Colours: the groups are `TypeTrackBad`, `TypeTrackMissing` and
`TypeTrackGhost` (grey, like comments). For example
`vim.api.nvim_set_hl(0, "TypeTrackGhost", { fg = "#5f5f5f" })`.

Good to know:

- Ghost text sits in front of the cursor at the end of the file, so
  type at the end; fixing an earlier word works as usual.
- Ghost text needs Neovim 0.10 or newer to sit in the line (0.11 is
  fine). It was tested headless; how it looks while typing in st has not
  been seen yet, so tell Claude if it lines up wrong.

### Marking the words in Sioyek too (off)

The old Sioyek part is still there but off, because Sioyek's development
version crashed doing it. With `require("typetrack").opts.sioyek_marks =
true` and a **development** Sioyek (Gentoo GURU's `app-text/sioyek-9999`;
release 2.0.0 cannot do it), Sioyek also highlights each book word green
(right) or red (wrong or skipped). Installing sioyek-9999 on Gentoo:

    sudo emerge --ask app-eselect/eselect-repository
    sudo eselect repository enable guru
    sudo emaint sync -r guru
    echo "app-text/sioyek **" | sudo tee /etc/portage/package.accept_keywords/sioyek
    sudo emerge --ask --update --deep --changed-use @world =app-text/sioyek-9999

Text already in the file is not sent to Sioyek the first time unless you
start with `:TypeTrack!`. Colours are Sioyek's highlight types `g` and
`r` (`highlight_color_g 0.30 0.80 0.35` in
`~/.config/sioyek/prefs_user.config`). Marks are real Sioyek highlights
and stay in the book; this has never been tried with a real Sioyek.
