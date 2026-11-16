-- ~/.config/nvim/init.lua
-- A small, close-to-default Neovim setup with a code mode and a prose mode,
-- made for writing fiction with stenography (Plover) on Gentoo Linux.
-- Written for Neovim 0.11 or newer. Plugins install themselves on first start.
--
-- Files in this config:
--   init.lua            this file (options, keys, Plover unicode fix,
--                       plugins, prose mode)
--   lua/typetrack.lua   optional: type a book over grey ghost text, mistakes red
-- See README.md for the user guide and HANDOFF.md for history, decisions
-- and open issues (read HANDOFF.md first if you are picking this work up).
--
-- Keys (leader is Space):
--   Space w  save                 Space p  prose mode on/off  (:Prose)
--   Space s  spell check on/off
--   Space t  book typing check on/off   Space T  same with ghost text
--            (:TypeTrack, :TypeGhost, :TypeChapter, :TypeTrackStop)
--   Space Space  open a file (fuzzy)    Space /  search text in files
--   Space b  pick an open buffer      Space h  search help pages
--   Esc      clear search highlight
--   sa/sd/sr add/delete/replace surrounding quotes or brackets (mini.surround)
--   Press Space (or g, z, ", ...) and wait: a box lists the keys.
--
-- Lives in the dotfiles repo (nvim/), linked to ~/.config/nvim by
-- install.sh. The user runs it on Neovim 0.11.7 in st (and tmux) under
-- dwm on X11. Problems reported and fixed: missing vim.pack on 0.11,
-- Pencil re-flowing single Enters, Plover symbols arriving as "2014".
-- st and tmux now pass Ctrl+Shift+U through (tested: it reaches Neovim
-- as <C-S-u>, in st and in st+tmux). Not yet confirmed by the user: a
-- full Plover symbol typed for real.

-- Keep the compiled form of every Lua file Neovim loads (this file, the
-- plugins) in ~/.system/cache/nvim/luac, so starting does not compile
-- them again. A file that changed is compiled again by itself.
vim.loader.enable()

-- Runtime plugins that are not used here are not loaded (about 4 ms at
-- every start): netrw (the file browser; Space Space opens files and
-- gx opens links without it), remote plugins, the tutor, and reading
-- inside zip and tar files. To get one back, delete its line.
vim.g.loaded_netrw = 1
vim.g.loaded_netrwPlugin = 1
vim.g.loaded_remote_plugins = 1
vim.g.loaded_tutor_mode_plugin = 1
vim.g.loaded_zipPlugin = 1
vim.g.loaded_tarPlugin = 1

vim.g.mapleader = " "          -- Space is the leader key

---------------------------------------------------------------------------
-- Everyday options (code mode is simply "the defaults")
---------------------------------------------------------------------------
local o = vim.opt

o.number = true                -- show the real number on the current line
o.relativenumber = true        -- other lines show distance (easy 5j, 12k)
o.colorcolumn = "80"           -- vertical guide at column 80
o.cursorline = true            -- highlight the line you are on
o.cursorlineopt = "number"     -- ...but only its number, not the whole row
o.signcolumn = "no"            -- no extra gutter on the left

o.termguicolors = true
o.background = "dark"

o.tabstop = 4                  -- a tab looks like 4 spaces
o.shiftwidth = 4               -- >> and << move 4 spaces
o.expandtab = true             -- Tab key inserts spaces
o.softtabstop = 4              -- ...and Backspace takes 4 of them away at
                               -- once, so 4 spaces behave like one tab
o.shiftround = true            -- >> and << snap to a multiple of 4
-- Only what you type changes: existing files keep their real tabs.
-- Makefiles still get real tabs (Neovim's own make settings do that).
o.smartindent = true

-- Show whitespace that is easy to miss, dimmed (colour set below):
--   a real tab: a right arrow, then spaces to the tab's full width
--   trailing spaces: middle dots   no-break space: an open-box sign
--   line goes on past the edge: single guillemets at either end
-- Trailing dots are hidden while typing in insert mode (the space before
-- your next word would flash otherwise). Prose mode hides all of this.
o.list = true
-- The characters are written as UTF-8 byte escapes (source stays ASCII):
-- right arrow, middle dot, open box, right and left single guillemet.
local listchars = "tab:\226\134\146 ,trail:\194\183,nbsp:\226\144\163,"
  .. "extends:\226\128\186,precedes:\226\128\185"
o.listchars = listchars
vim.api.nvim_create_autocmd("InsertEnter", {
  callback = function()
    if vim.wo.list then vim.opt_local.listchars:remove("trail") end
  end,
})
vim.api.nvim_create_autocmd("InsertLeave", {
  callback = function() vim.opt_local.listchars = listchars end,
})

o.inccommand = "split"         -- :s/old/new shows the changes while you
                               -- type, with a list of all hits below
o.ignorecase = true            -- /foo matches Foo...
o.smartcase = true             -- ...unless you type a capital letter

o.scrolloff = 5                -- keep a few lines visible above/below cursor
o.wrap = true                  -- long lines fold onto the next screen row
o.linebreak = true             -- ...between words, not mid-word
o.breakindent = true           -- ...indented like the line they continue
                               -- (still one line in the file; the screen
                               -- never scrolls sideways)
o.splitright = true
o.splitbelow = true
o.mouse = "a"
o.clipboard = "unnamedplus"    -- yank/paste uses the system clipboard
                               -- (needs xclip or xsel; install.sh --deps
                               -- installs both)
o.updatetime = 300

-- Persistent history. Neovim keeps these in its own state directory
-- (~/.system/state/nvim/) so your project directories stay clean.
local state = vim.fn.stdpath("state")
o.undofile = true              -- undo survives closing the file
o.undodir = state .. "/undo//" -- ...stored here, one file per edited file
o.viewdir = state .. "/view"   -- saved folds and cursor per file
o.viewoptions = "folds,cursor" -- what a saved view remembers
o.swapfile = false             -- no .swp files
o.shada = "!,'200,<50,s10,h"   -- remember marks, registers, command history

-- Remember folds (and cursor) per file: saved when you leave a file,
-- restored when you open it again.
local view_group = vim.api.nvim_create_augroup("RememberView", { clear = true })
local function is_real_file(buf)
  return vim.bo[buf].buftype == "" and vim.api.nvim_buf_get_name(buf) ~= ""
end
vim.api.nvim_create_autocmd({ "BufWinLeave", "BufWritePost" }, {
  group = view_group,
  callback = function(args)
    if is_real_file(args.buf) then pcall(vim.cmd.mkview) end
  end,
})
vim.api.nvim_create_autocmd("BufWinEnter", {
  group = view_group,
  callback = function(args)
    if is_real_file(args.buf) then pcall(vim.cmd, "silent! loadview") end
  end,
})

-- Colour scheme: Habamax, a calm dark theme that ships with Neovim.
vim.cmd.colorscheme("habamax")
-- Tabs, trailing spaces etc. (see listchars above) in a dark grey, about
-- half as bright as normal text, so you see them without being bothered.
vim.api.nvim_set_hl(0, "Whitespace", { fg = "#4e4e4e" })
vim.api.nvim_set_hl(0, "NonText", { fg = "#4e4e4e" })

---------------------------------------------------------------------------
-- Small key mappings
---------------------------------------------------------------------------
local map = vim.keymap.set
map("n", "<Esc>", "<cmd>nohlsearch<CR>", { desc = "Clear search highlight" })
map("n", "<leader>w", "<cmd>write<CR>", { desc = "Save" })

-- Symbols from steno (em and en dash, curly quotes, ellipsis). On Linux, Plover types a symbol the way
-- GTK apps expect: Ctrl+Shift+U, its hex code (2014), then Space/Enter.
-- Terminals ignore that shortcut, so only "2014" arrived. This catches
-- the sequence in insert mode and puts in the real character instead.
-- Up to 6 hex digits are read; Space or Enter ends the code and is
-- dropped, any other key ends it and is typed as usual.
-- The terminal has to send Ctrl+Shift+U as its own key: st's config.h
-- maps it to CSI u (ESC [117;6u), and tmux.conf turns on extended-keys
-- so it also gets through tmux. Plain Ctrl+U is unchanged.
map("i", "<C-S-u>", function()
  local hex = ""
  while true do
    local c = vim.fn.getcharstr()
    if c:match("^%x$") and #hex < 6 then
      hex = hex .. c
    else
      local ch = #hex > 0 and vim.fn.nr2char(tonumber(hex, 16)) or ""
      if c ~= " " and c ~= "\r" then ch = ch .. c end  -- keep other keys
      return ch
    end
  end
end, { expr = true, replace_keycodes = false, desc = "Unicode from steno" })

-- Briefly highlight what you yank
vim.api.nvim_create_autocmd("TextYankPost", {
  callback = function() vim.hl.on_yank() end,
})

-- Come back to the last cursor position when reopening a file
vim.api.nvim_create_autocmd("BufReadPost", {
  callback = function(args)
    local mark = vim.api.nvim_buf_get_mark(args.buf, '"')
    if mark[1] > 0 and mark[1] <= vim.api.nvim_buf_line_count(args.buf) then
      pcall(vim.api.nvim_win_set_cursor, 0, mark)
    end
  end,
})

-- Spelling comes from steno, so spell check is off; <Space>s toggles it.
o.spelllang = "en"
map("n", "<leader>s", "<cmd>setlocal spell!<CR>", { desc = "Toggle spell check" })

-- In prose files, wrap at 80 even outside prose mode (for gq and typing).
vim.api.nvim_create_autocmd("FileType", {
  pattern = { "markdown", "text", "gitcommit" },
  callback = function()
    vim.opt_local.textwidth = 80
  end,
})

---------------------------------------------------------------------------
-- Type a book out (the code is in lua/typetrack.lua)
--   :TypeGhost ~/books/book.epub   ghost mode: the book's next words are
--                                  grey after the cursor; type over them
--   :TypeTrack ~/books/book.epub   check mode: same checks, no ghosts
--   Both: wrong letters and extra words red, skipped words and letters
--   shown crossed out in red.
--   :TypeChapter                   pick the chapter you start at
--   Space t / Space T              check / ghost mode on/off for this file
--                                  (remembers the book and the place)
--   :TypeTrackStop                 stop
--   Optional: if lua/typetrack.lua is missing, none of this is defined
--   and the rest of the config works as usual. Needs unzip (EPUB) or
--   pdftotext (PDF).
---------------------------------------------------------------------------
local has_typetrack, typetrack = pcall(require, "typetrack")
if has_typetrack then
  vim.api.nvim_create_user_command("TypeTrack", function(a)
    typetrack.start(a.args, a.bang, "check")
  end, { nargs = "?", bang = true, complete = "file" })
  vim.api.nvim_create_user_command("TypeGhost", function(a)
    typetrack.start(a.args, a.bang, "ghost")
  end, { nargs = "?", bang = true, complete = "file" })
  vim.api.nvim_create_user_command("TypeChapter", function()
    typetrack.choose_start()
  end, {})
  vim.api.nvim_create_user_command("TypeTrackStop", function()
    typetrack.stop()
  end, {})
  local function toggle(mode)
    return function()
      if typetrack.mode() == mode then typetrack.stop() else typetrack.start(nil, false, mode) end
    end
  end
  map("n", "<leader>t", toggle("check"), { desc = "Check typing against the book" })
  map("n", "<leader>T", toggle("ghost"), { desc = "Type over the book's ghost text" })
end

---------------------------------------------------------------------------
-- Plugins (installed automatically on first start)
---------------------------------------------------------------------------
-- No plugin manager: each plugin is cloned once with git into Neovim's
-- own package directory and loaded with :packadd. Works on Neovim 0.11+.
-- (vim.pack was tried first but needs 0.12; the user is on 0.11.7.
-- To update the plugins, delete the pack_dir directory and restart.)
-- Everything that must work without plugins sits ABOVE this section,
-- because the file stops at the `return` below if a download fails.
local pack_dir = vim.fn.stdpath("data") .. "/site/pack/plugins/opt/"
local function plugin(repo, branch)
  local name = repo:match("[^/]+$")
  local path = pack_dir .. name
  if not vim.uv.fs_stat(path) then
    vim.notify("Installing " .. name .. "...")
    local cmd = { "git", "clone", "--depth=1" }
    if branch then vim.list_extend(cmd, { "--branch", branch }) end
    vim.list_extend(cmd, { "https://github.com/" .. repo, path })
    local out = vim.fn.system(cmd)
    if vim.v.shell_error ~= 0 then
      vim.notify("Could not install " .. name .. ":\n" .. out,
        vim.log.levels.ERROR)
      return false
    end
    pcall(vim.cmd, "silent! helptags " .. vim.fn.fnameescape(path .. "/doc"))
  end
  vim.cmd.packadd(name)
  return true
end

local have_zen = plugin("folke/zen-mode.nvim")       -- distraction-free view
local have_pencil = plugin("preservim/vim-pencil")   -- wrapping for prose
-- Lessons and cheatsheet for steno Vim commands (plover-vim). Help pages
-- only, it changes nothing else: :help plover-vim-tutorial to start.
-- The steno side is installed in Plover itself, not here.
-- The user is NOT using plover-vim for now (it needs Emily's modifiers,
-- which they don't want to learn yet) but asked to keep this line.
plugin("Josiah-tan/plover-vim-tutor")

-- Fuzzy finder (mini.pick): type a few letters of a name, Enter opens.
-- Inside the list: Ctrl+n / Ctrl+p (or arrows) move, Esc closes,
-- Ctrl+s / Ctrl+v open in a split. Space / searches inside files: it
-- uses ripgrep (rg) if installed, otherwise only works in a git directory.
if plugin("nvim-mini/mini.pick") then
  -- set up at the first use, not at every start
  local pick_ready = false
  local function pick(name)
    return function()
      local mp = require("mini.pick")
      if not pick_ready then
        mp.setup()
        pick_ready = true
      end
      mp.builtin[name]()
    end
  end
  map("n", "<leader><leader>", pick("files"), { desc = "Open file" })
  map("n", "<leader>/", pick("grep_live"), { desc = "Search text in files" })
  map("n", "<leader>b", pick("buffers"), { desc = "Pick open buffer" })
  map("n", "<leader>h", pick("help"), { desc = "Search help" })
end

-- Surround (mini.surround): add, delete or replace quotes and brackets.
--   saiw"   add " around the word        sd"   delete the " around
--   sr"'    replace " with '             sa$)  add ( ) to end of line
-- (Plain s, "delete letter and type", is still cl.)
if plugin("nvim-mini/mini.surround") then
  -- set up right after the first screen is drawn, not before it
  vim.api.nvim_create_autocmd("UIEnter", {
    once = true,
    callback = vim.schedule_wrap(function()
      require("mini.surround").setup()
    end),
  })
end

-- which-key: press Space (or g, z, ", Ctrl+w ...) and wait a moment; a
-- small box lists what can follow, with the descriptions given here.
-- Also set up after the first screen.
if plugin("folke/which-key.nvim") then
  vim.api.nvim_create_autocmd("UIEnter", {
    once = true,
    callback = vim.schedule_wrap(function()
      require("which-key").setup()
    end),
  })
end

-- Without zen-mode and pencil there is no prose mode, so stop here.
if not (have_zen and have_pencil) then return end

---------------------------------------------------------------------------
-- Prose mode
--   :Prose or <Space>p toggles it.
--   zen-mode gives a centred 82-wide window without numbers, guide or
--   status bar; vim-pencil hard-wraps at 80 (real line breaks in the
--   file, so wrap points stay put between sessions).
--   Inside it, j/k and the arrows move by screen line on long lines
--   (a count like 5j still moves real lines). That is undone on leaving,
--   so code mode keeps normal movement.
--   Markdown and text files also wrap at 80 by default (see above).
--   Spelling comes from steno, so spell check is off; <Space>s toggles it.
---------------------------------------------------------------------------
vim.g["pencil#wrapModeDefault"] = "hard"  -- real line breaks at 80
vim.g["pencil#textwidth"] = 80
vim.g["pencil#conceallevel"] = 0
-- No auto-reflow: a single Enter starts a new line and stays there
-- (book-style paragraphs). Lines still break at 80 while you type;
-- after editing the middle of a paragraph, gqip re-wraps it.
vim.g["pencil#autoformat"] = 0

local prose_buf = nil
require("zen-mode").setup({
  window = {
    width = 82,                 -- 80 characters plus a little breathing room
    backdrop = 1,
    options = {
      number = false,
      relativenumber = false,
      colorcolumn = "",
      cursorline = false,
      signcolumn = "no",
      foldcolumn = "0",
      list = false,
    },
  },
  plugins = {
    options = { laststatus = 0, ruler = false, showcmd = false },
  },
  on_open = function()
    prose_buf = vim.api.nvim_get_current_buf()
    vim.cmd("Pencil")
    -- Long lines (e.g. a whole paragraph on one line) wrap on screen at
    -- word boundaries, and the arrows / j k move by screen line, not by
    -- paragraph. With a count (5j) they still jump real lines.
    vim.wo.wrap = true
    vim.wo.linebreak = true
    for _, key in ipairs({ "j", "<Down>" }) do
      vim.keymap.set({ "n", "x" }, key, "v:count ? 'j' : 'gj'",
        { buffer = true, expr = true })
    end
    for _, key in ipairs({ "k", "<Up>" }) do
      vim.keymap.set({ "n", "x" }, key, "v:count ? 'k' : 'gk'",
        { buffer = true, expr = true })
    end
    vim.keymap.set("i", "<Down>", "<C-o>gj", { buffer = true })
    vim.keymap.set("i", "<Up>", "<C-o>gk", { buffer = true })
  end,
  on_close = function()
    vim.cmd("NoPencil")
    -- Back to code mode: remove the screen-line arrows from the buffer
    -- that was in prose mode (wrap goes away with the prose window).
    local b = prose_buf or 0
    for _, key in ipairs({ "j", "k", "<Down>", "<Up>" }) do
      pcall(vim.keymap.del, "n", key, { buffer = b })
      pcall(vim.keymap.del, "x", key, { buffer = b })
    end
    pcall(vim.keymap.del, "i", "<Down>", { buffer = b })
    pcall(vim.keymap.del, "i", "<Up>", { buffer = b })
  end,
})

local function toggle_prose() require("zen-mode").toggle() end
vim.api.nvim_create_user_command("Prose", toggle_prose, {})
map("n", "<leader>p", toggle_prose, { desc = "Toggle prose mode" })

-- :q, :wq and :x in prose mode leave Neovim, as they do anywhere else.
-- On their own they only closed the prose window and left the file open.
vim.api.nvim_create_autocmd("QuitPre", {
  callback = function()
    local view = require("zen-mode.view")
    if not view.is_open() then return end
    -- the ! of :q! or :wq! (the last command line typed)
    local bang = vim.fn.histget(":", -1):match("^%s*%a+!") ~= nil
    view.close()
    vim.schedule(function()
      if #vim.api.nvim_list_wins() ~= 1 then return end
      vim.cmd(bang and "quit!" or "quit")
    end)
  end,
})
