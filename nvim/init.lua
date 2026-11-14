-- ~/.config/nvim/init.lua
-- A small, close-to-default Neovim setup with a code mode and a prose mode,
-- made for writing fiction with stenography (Plover) on Gentoo Linux.
-- Written for Neovim 0.11 or newer. Plugins install themselves on first start.
--
-- Files in this config:
--   init.lua            this file (options, keys, Harper, Claude proofread,
--                       Plover unicode fix, plugins, prose mode)
--   lua/typetrack.lua   optional: type a book over grey ghost text, mistakes red
-- See README.md for the user guide and HANDOFF.md for history, decisions
-- and open issues (read HANDOFF.md first if you are picking this work up).
--
-- Keys (leader is Space):
--   Space w  save                 Space p  prose mode on/off  (:Prose)
--   Space s  spell check on/off   Space g  proofread file with Claude
--   Space f  apply Claude's fix   Space G  clear Claude marks (:ClaudeLog)
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
-- Pencil re-flowing single Enters, Claude over-marking lines, Plover
-- symbols arriving as "2014". st and tmux now pass Ctrl+Shift+U through
-- (tested: it reaches Neovim as <C-S-u>, in st and in st+tmux). Not yet
-- confirmed by the user: the tightened Claude prompt, and a full Plover
-- symbol typed for real.

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
--   a real tab   →   (an arrow, then spaces to the tab's full width)
--   trailing spaces ·   no-break space ␣   line goes on past the edge › ‹
-- Trailing dots are hidden while typing in insert mode (the space before
-- your next word would flash otherwise). Prose mode hides all of this.
o.list = true
local listchars = "tab:→ ,trail:·,nbsp:␣,extends:›,precedes:‹"
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

-- Persistent history. Neovim keeps these in its own state folder
-- (~/.system/state/nvim/) so your project folders stay clean.
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

-- Symbols from steno (— – “ ” …). On Linux, Plover types a symbol the way
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

---------------------------------------------------------------------------
-- Grammar check for prose: Harper (https://writewithharper.com)
--   Runs offline, no plugin needed, uses Neovim's built-in LSP client.
--   Only starts if the harper-ls program is installed
--   (cargo install harper-ls --locked). Harper's own spell check is off.
--   ]d / [d jump between issues, the message pops up when the cursor
--   rests on one (or <C-w>d), gra offers fixes.
---------------------------------------------------------------------------
if vim.fn.executable("harper-ls") == 1 then
  vim.lsp.config("harper_ls", {
    cmd = { "harper-ls", "--stdio" },
    filetypes = { "markdown", "text", "gitcommit" },
    root_markers = { ".git" },
    settings = {
      ["harper-ls"] = {
        linters = { SpellCheck = false },  -- spelling comes from steno
      },
    },
  })
  vim.lsp.enable("harper_ls")
end
-- Problems are underlined; the explanation pops up when the cursor rests
-- on an underlined word (no text squeezed in at the end of lines, which
-- prose mode's narrow window would cut off). Applies to Harper and to
-- the Claude marks below.
vim.diagnostic.config({
  virtual_text = false,
  signs = false,
  underline = true,
  float = { border = "rounded", source = true },
})
vim.api.nvim_create_autocmd("CursorHold", {
  callback = function()
    vim.diagnostic.open_float({ scope = "cursor", focus = false })
  end,
})

---------------------------------------------------------------------------
-- Ask Claude to proofread: <Space>g
--   Sends ONLY the text of the file you are in (piped in, not a path).
--   Claude gets no tools at all, so it cannot open or read any other file,
--   run commands or browse. It runs from an empty temporary folder.
--   API-key variables are removed so it always uses the Claude account you
--   signed in with (`claude` then /login, done once).
--   The wrong words get underlined. Rest the cursor on them and the
--   explanation and suggested fix pop up. <Space>f applies the fix.
--   ]d / [d jump between marks, <Space>g re-checks, <Space>G clears.
--   :ClaudeLog opens Claude's last raw answer, for debugging the prompt.
--   The flags below were checked against `claude --help`. The user has
--   run Space g and it worked, but it over-marked fine lines; the prompt
--   and filter were tightened after that, not yet re-tested by the user.
---------------------------------------------------------------------------
local claude_ns = vim.api.nvim_create_namespace("claude_check")
-- "sonnet" is the best balance of quality and cost for proofreading
-- (the user chose it). "haiku" is cheaper but understands meaning less.
local claude_model = "sonnet"
-- Claude's last answer is saved here (on your computer only).
local claude_log = vim.fn.stdpath("state") .. "/claude-check.log"
vim.api.nvim_create_user_command("ClaudeLog", function()
  vim.cmd("split " .. vim.fn.fnameescape(claude_log))
end, {})

local claude_prompt = [[
You are proofreading the text you receive. Each line starts with its line
number and a colon. Look for grammar mistakes, wrong words (the author
writes with stenography, so watch for misstrokes that produce real but
wrong words), missing or doubled words, and sentences that are unclear or
do not make sense. Ignore Markdown syntax and the line numbers themselves.
This is often fiction: informal phrasing, contractions, sentence fragments
and an author's style are NOT mistakes. Report only clear errors you are
sure about. Never report a line just to say it is fine, and never suggest
a "fix" that is the same as the original. Most lines need no report.
Reply ONLY with lines in this exact form, one problem per line:
N | exact wrong words | corrected words | short explanation
"exact wrong words" must be copied exactly from line N (a few words, not
the whole line). "corrected words" replaces just those words.
If you find nothing, reply with nothing at all.]]

local function claude_check()
  if vim.fn.executable("claude") == 0 then
    vim.notify("Claude Code (`claude`) is not installed.", vim.log.levels.WARN)
    return
  end
  local buf = vim.api.nvim_get_current_buf()
  local lines = vim.api.nvim_buf_get_lines(buf, 0, -1, false)
  local numbered = {}
  for i, line in ipairs(lines) do numbered[i] = i .. ": " .. line end

  local empty_dir = vim.fn.tempname()
  vim.fn.mkdir(empty_dir, "p")
  vim.notify("Claude is checking this file...")

  vim.system({
    "env", "-u", "ANTHROPIC_API_KEY", "-u", "ANTHROPIC_AUTH_TOKEN",
    "claude", "-p", "Proofread this text.",
    "--system-prompt", claude_prompt, -- short prompt instead of the big
                                      -- coding one: uses far less of
                                      -- your Claude allowance per check
    "--model", claude_model,
    "--effort", "low",                -- quick answer, no long thinking
    "--tools", "",                    -- no tools: no file reading, no commands
    "--strict-mcp-config",            -- no connectors / MCP servers either
    "--disable-slash-commands",
    "--no-session-persistence",       -- the check is not saved as a session
    "--output-format", "text",
  }, { stdin = table.concat(numbered, "\n"), cwd = empty_dir, text = true },
  vim.schedule_wrap(function(result)
    vim.fn.delete(empty_dir, "rf")
    if result.code ~= 0 then
      vim.notify("Claude check failed:\n" .. (result.stderr or ""),
        vim.log.levels.ERROR)
      return
    end
    -- Keep Claude's last raw answer, readable with :ClaudeLog
    local out = result.stdout or ""
    vim.fn.writefile(vim.split(out, "\n"), claude_log)
    if not vim.api.nvim_buf_is_valid(buf) then return end
    local marks = {}
    for reply in out:gmatch("[^\n]+") do
      local n, wrong, fix, why =
        reply:match("^%s*(%d+)%s*|%s*(.-)%s*|%s*(.-)%s*|%s*(.-)%s*$")
      n = tonumber(n)
      -- Keep only real suggestions whose words are found on that line,
      -- so only those words are underlined (never a whole line).
      local from, to
      if n and n >= 1 and n <= #lines and wrong ~= "" and fix ~= wrong then
        from, to = lines[n]:find(wrong, 1, true)
      end
      if from then
        table.insert(marks, {
          lnum = n - 1, col = from - 1, end_col = to,
          severity = vim.diagnostic.severity.WARN,
          message = why .. '\n"' .. wrong .. '" -> "' .. fix .. '"'
            .. "\n(Space f applies this fix)",
          source = "Claude",
          user_data = { wrong = wrong, fix = fix },
        })
      end
    end
    vim.diagnostic.set(claude_ns, buf, marks)
    if #marks == 0 then
      vim.notify("Claude found no problems.")
    else
      vim.notify("Claude marked " .. #marks .. " line(s). Use ]d / [d.")
    end
  end))
end

map("n", "<leader>g", claude_check, { desc = "Proofread file with Claude" })
map("n", "<leader>G", function() vim.diagnostic.reset(claude_ns, 0) end,
  { desc = "Clear Claude marks" })

-- <Space>f: apply Claude's suggested fix for the mark under the cursor.
local function claude_fix()
  local row, col = unpack(vim.api.nvim_win_get_cursor(0))
  local here = vim.diagnostic.get(0, { namespace = claude_ns, lnum = row - 1 })
  if #here == 0 then
    vim.notify("No Claude mark on this line.")
    return
  end
  local pick = here[1]   -- prefer the mark under the cursor
  for _, d in ipairs(here) do
    if col >= d.col and col <= d.end_col then pick = d end
  end
  local data = pick.user_data or {}
  local line = vim.api.nvim_get_current_line()
  local from, to = line:find(data.wrong or "", 1, true)
  if not data.wrong or data.wrong == "" or not from then
    vim.notify("Can't find the marked words any more; fix it by hand.")
    return
  end
  vim.api.nvim_buf_set_text(0, row - 1, from - 1, row - 1, to, { data.fix })
  local rest = {}
  for _, d in ipairs(vim.diagnostic.get(0, { namespace = claude_ns })) do
    if not (d.lnum == pick.lnum and d.col == pick.col
        and d.message == pick.message) then
      table.insert(rest, d)
    end
  end
  vim.diagnostic.set(claude_ns, 0, rest)
end
map("n", "<leader>f", claude_fix, { desc = "Apply Claude's fix" })

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
-- own package folder and loaded with :packadd. Works on Neovim 0.11+.
-- (vim.pack was tried first but needs 0.12; the user is on 0.11.7.
-- To update the plugins, delete the pack_dir folder and restart.)
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

-- Syntax highlighting with Tree-sitter: more accurate colours than
-- Vim's built-in syntax files. Each language needs a small "parser",
-- compiled once on your computer (needs gcc, which Gentoo has). The
-- common languages below are built in the background on first start;
-- any other language is built the first time you open such a file.
-- Languages without a parser still get Vim's built-in highlighting,
-- which covers hundreds of file types.
-- Only highlighting is on: no error checking, no completion.
-- The "master" branch is the one for Neovim 0.11 (the newer "main"
-- branch needs 0.12). After moving to Neovim 0.12, switch to "main".
-- After updating the plugin, run :TSUpdate to rebuild the parsers.
if plugin("nvim-treesitter/nvim-treesitter", "master") then
  require("nvim-treesitter.configs").setup({
    ensure_installed = {
      "bash", "c", "cpp", "css", "csv", "diff", "dockerfile",
      "git_config", "git_rebase", "gitcommit", "gitignore", "go", "html",
      "ini", "java", "javascript", "jsdoc", "json", "lua", "luadoc",
      "make", "markdown", "markdown_inline", "perl", "php", "python",
      "query", "regex", "ruby", "rust", "sql", "toml", "tsx",
      "typescript", "vim", "vimdoc", "xml", "yaml",
    },
    auto_install = true,        -- others: built when you open such a file
    -- These need an extra program (tree-sitter-cli) to build, so they
    -- keep Vim's built-in highlighting instead.
    ignore_install = { "latex", "mlir", "ocamllex", "scfg", "swift",
      "teal", "unison" },
    highlight = { enable = true },
  })
end

-- Fuzzy finder (mini.pick): type a few letters of a name, Enter opens.
-- Inside the list: Ctrl+n / Ctrl+p (or arrows) move, Esc closes,
-- Ctrl+s / Ctrl+v open in a split. Space / searches inside files: it
-- uses ripgrep (rg) if installed, otherwise only works in a git folder.
if plugin("nvim-mini/mini.pick") then
  local pick = require("mini.pick")
  pick.setup()
  map("n", "<leader><leader>", pick.builtin.files, { desc = "Open file" })
  map("n", "<leader>/", pick.builtin.grep_live, { desc = "Search text in files" })
  map("n", "<leader>b", pick.builtin.buffers, { desc = "Pick open buffer" })
  map("n", "<leader>h", pick.builtin.help, { desc = "Search help" })
end

-- Surround (mini.surround): add, delete or replace quotes and brackets.
--   saiw"   add " around the word        sd"   delete the " around
--   sr"'    replace " with '             sa$)  add ( ) to end of line
-- (Plain s, "delete letter and type", is still cl.)
if plugin("nvim-mini/mini.surround") then
  require("mini.surround").setup()
end

-- which-key: press Space (or g, z, ", Ctrl+w ...) and wait a moment; a
-- small box lists what can follow, with the descriptions given here.
if plugin("folke/which-key.nvim") then
  require("which-key").setup()
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
