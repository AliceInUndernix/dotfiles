-- ~/.config/nvim/lua/typetrack.lua
-- Type a book out in Neovim and every mistake shows up as you type:
-- wrong letters turn red, extra words turn red, and book words you
-- skipped appear crossed out in red where they should have been.
--
-- Two modes:
--   ghost mode   the book's next words are shown in grey after the
--                cursor ("ghosts": not part of the file, cannot be
--                edited). You type over them.
--   check mode   the same checking, no ghosts.
--
--   :TypeGhost ~/books/book.epub   start ghost mode for the file you are in
--   :TypeTrack ~/books/book.epub   start check mode
--   :TypeGhost / :TypeTrack        start again (remembers book and place)
--   :TypeChapter                   pick where in the book you start
--                                  (skips cover, contents, foreword...)
--   :TypeTrackStop                 stop
--
-- Needs `unzip` for EPUB or `pdftotext` (poppler) for PDF. Sioyek is not
-- needed unless opts.sioyek_marks is turned on (see below).
--
-- How it works:
--   1. The book's text is read by Neovim itself (EPUB: unzip the chapters
--      in spine order and strip the HTML; PDF: pdftotext, page by page)
--      and split into words. The EPUB table of contents gives the
--      chapter list for :TypeChapter (PDF: one entry per page).
--   2. On every change, the buffer's words are lined up with the book
--      from the chosen start: align() walks along word by word; where a
--      word does not match it weighs "typo", "skipped book words" and
--      "extra typed word" by how well the next few words agree.
--   3. Each typed word is compared letter by letter with its book word
--      (char_diff): wrong or extra letters are red, missing letters are
--      shown crossed out in red. The word you are still typing is
--      compared letter by letter as far as you have got.
--   4. Ghost mode puts the rest of the current book word and the next
--      words as grey virtual text at the end of the file, wrapped at
--      'textwidth' (80), with a few more lines below.
--   5. Book, mode and start are saved per text file in
--      stdpath("state")/typetrack.json, so it carries on next time.
--   6. Optional (opts.sioyek_marks): runs of right/wrong book words are
--      highlighted in Sioyek with `sioyek --execute-command`. Off since
--      Sioyek's development version crashed doing it (2026-11-12).
--
-- Testing status (2026-11-14): steps 1-5 were run headless on Neovim
-- 0.9.5 with a test EPUB (no 0.11 was available to the author). Inline
-- ghost text needs Neovim 0.10+; on 0.9 it falls back to the end of the
-- line. How the inline ghosts look while typing in a real terminal was
-- NOT seen. Step 6 has never run against a real Sioyek.
--
-- M._internal at the bottom exposes the pieces for headless tests.

local M = {}
local uv = vim.uv or vim.loop
local INLINE = vim.fn.has("nvim-0.10") == 1

M.opts = {
  strict = true,         -- capitals and punctuation must match too
  pause_ms = 1500,       -- check mode: after this long without typing,
                         -- the last word counts as finished
  ghost_lines = 4,       -- ghost mode: lines of book text shown below
                         -- the line you are typing on
  width = nil,           -- ghost line width; nil = 'textwidth' or 80
  sioyek = "sioyek",     -- the sioyek program
  sioyek_marks = false,  -- colour the words in Sioyek too
  good = "g",            -- Sioyek highlight type for right words (green)
  bad = "r",             -- ...and for wrong or skipped words (red)
  max_run = 12,          -- longest single Sioyek highlight, in words
}

local ns = vim.api.nvim_create_namespace("typetrack")

local function set_hl()
  vim.api.nvim_set_hl(0, "TypeTrackBad",
    { default = true, fg = "#ff5f5f", underline = true, sp = "#ff5f5f" })
  vim.api.nvim_set_hl(0, "TypeTrackMissing",
    { default = true, fg = "#ff5f5f", strikethrough = true })
  vim.api.nvim_set_hl(0, "TypeTrackGhost", { default = true, link = "Comment" })
end
set_hl()
vim.api.nvim_create_autocmd("ColorScheme", {
  group = vim.api.nvim_create_augroup("TypeTrackColours", { clear = true }),
  callback = set_hl,
})

---------------------------------------------------------------------------
-- Words and letters
---------------------------------------------------------------------------

-- Typographic characters that should count the same as plain ones.
local SAME = {
  ["\226\128\152"] = "'", ["\226\128\153"] = "'", ["\202\188"] = "'", -- ‘ ’ ʼ
  ["\226\128\156"] = '"', ["\226\128\157"] = '"', ["\226\128\158"] = '"', -- “ ” „
  ["\194\171"] = '"', ["\194\187"] = '"',                              -- « »
  ["\226\128\166"] = "...",                                             -- …
}
local function normalise(s)
  for from, to in pairs(SAME) do s = s:gsub(from, to) end
  return s
end

-- The characters of a string (UTF-8), each with its byte offset.
local function chars(s)
  local list = {}
  for pos, c in s:gmatch("()([%z\1-\127\192-\255][\128-\191]*)") do
    list[#list + 1] = { c = c, at = pos - 1 }
  end
  return list
end

-- Dashes separate words ("word—word", "word -- word"). They are replaced
-- by the same number of spaces so byte positions in a line stay correct.
local function blank_dashes(s)
  s = s:gsub("\226\128\148", "   ")   -- — em dash
  s = s:gsub("\226\128\147", "   ")   -- – en dash
  s = s:gsub("%-%-+", function(d) return (" "):rep(#d) end)
  return s
end

-- One word: what it looks like, a loose key for lining words up, and its
-- letter groups ("don't" -> don, t) for finding it in Sioyek.
local function make_word(raw)
  local n = normalise(raw)
  local key = n:gsub("[^%w\128-\255]", ""):lower()
  if key == "" then return nil end           -- only punctuation
  local parts = {}
  for p in n:gmatch("[%w\128-\255]+") do parts[#parts + 1] = p end
  return { raw = raw, norm = n, key = key, parts = parts }
end

-- Words of a text. Each also keeps what came before it (`gap`: a space,
-- a dash, punctuation on its own) and whether it starts a paragraph, so
-- ghost text can be shown as the book prints it.
local function words_of(text)
  local list, prev = {}, 1
  for s, _, e in blank_dashes(text):gmatch("()(%S+)()") do
    local w = make_word(text:sub(s, e - 1))
    if w then
      local gap = text:sub(prev, s - 1)
      w.para = gap:find("\n[ \t\r]*\n") ~= nil
      w.gap = gap:gsub("%s+", " ")
      list[#list + 1] = w
      prev = e
    end
  end
  return list
end

-- Words of the buffer, with where each one sits.
local function buffer_words(buf)
  local list = {}
  for lnum, line in ipairs(vim.api.nvim_buf_get_lines(buf, 0, -1, false)) do
    for s, _, e in blank_dashes(line):gmatch("()(%S+)()") do
      local w = make_word(line:sub(s, e - 1))
      if w then
        w.lnum, w.col, w.ecol = lnum - 1, s - 1, e - 1
        list[#list + 1] = w
      end
    end
  end
  return list
end

local function letter(c)
  c = SAME[c] or c
  if not M.opts.strict then c = c:lower() end
  return c
end

-- Edit distance between two lists of letters (a swap of two neighbours
-- counts as one edit), with the table for tracing it back.
local function distance(a, b)
  local d = {}
  for i = 0, #a do d[i] = { [0] = i } end
  for j = 0, #b do d[0][j] = j end
  for i = 1, #a do
    for j = 1, #b do
      local v = math.min(d[i - 1][j] + 1, d[i][j - 1] + 1,
        d[i - 1][j - 1] + (a[i] == b[j] and 0 or 1))
      if i > 1 and j > 1 and a[i] == b[j - 1] and a[i - 1] == b[j] then
        v = math.min(v, d[i - 2][j - 2] + 1)
      end
      d[i][j] = v
    end
  end
  return d[#a][#b], d
end

local function letters_of(s)
  local list = {}
  for _, ch in ipairs(chars(s)) do list[#list + 1] = letter(ch.c) end
  return list
end

-- Compare a finished typed word with its book word, letter by letter.
-- Returns bad = { [i] = true } for typed letters that are wrong or extra,
-- and missing = { [i] = "xy" }: book letters not typed, which belong
-- just before typed letter i (i = #typed + 1: at the end).
local function char_diff(typed_raw, book_raw)
  local a, b = letters_of(typed_raw), letters_of(book_raw)
  local _, d = distance(a, b)
  local bad, missing = {}, {}
  local i, j = #a, #b
  local bc = chars(book_raw)
  while i > 0 or j > 0 do
    if i > 0 and j > 0 and d[i][j] == d[i - 1][j - 1] + (a[i] == b[j] and 0 or 1) then
      if a[i] ~= b[j] then bad[i] = true end
      i, j = i - 1, j - 1
    elseif i > 1 and j > 1 and a[i] == b[j - 1] and a[i - 1] == b[j]
        and d[i][j] == d[i - 2][j - 2] + 1 then
      bad[i], bad[i - 1] = true, true
      i, j = i - 2, j - 2
    elseif i > 0 and d[i][j] == d[i - 1][j] + 1 then
      bad[i] = true                      -- a letter too many
      i = i - 1
    else
      missing[i + 1] = bc[j].c .. (missing[i + 1] or "")   -- a letter left out
      j = j - 1
    end
  end
  return bad, missing
end

-- The word still being typed: letter by letter as far as it goes.
local function prefix_diff(typed_raw, book_raw)
  local a, b = letters_of(typed_raw), letters_of(book_raw)
  local bad = {}
  for i = 1, #a do
    if a[i] ~= b[i] then bad[i] = true end
  end
  return bad
end

local function same(a, b)
  if M.opts.strict then return a.norm == b.norm end
  return a.key == b.key
end

-- Close enough to be the same word with a typo or two?
local function close(a, b)
  local la, lb = vim.fn.strchars(a.key), vim.fn.strchars(b.key)
  local allowed = math.max(1, math.floor(math.max(la, lb) / 3))
  if math.abs(la - lb) > allowed then return false end
  return (distance(letters_of(a.key), letters_of(b.key))) <= allowed
end

---------------------------------------------------------------------------
-- Reading the book
---------------------------------------------------------------------------

local ENTITIES = {
  amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = " ",
  mdash = "—", ndash = "–", hellip = "…", lsquo = "‘", rsquo = "’",
  ldquo = "“", rdquo = "”", shy = "", copy = "©", reg = "®",
  trade = "™", times = "×", middot = "·", bull = "•", laquo = "«",
  raquo = "»", bdquo = "„", sbquo = "‚", thinsp = " ", ensp = " ",
  emsp = " ", zwj = "", zwnj = "",
}
local BLOCK = {}
for _, t in ipairs({ "p", "div", "h1", "h2", "h3", "h4", "h5", "h6",
  "li", "ul", "ol", "tr", "td", "th", "table", "blockquote", "section",
  "article", "header", "footer", "aside", "pre", "hr", "dt", "dd", "figure",
  "figcaption", "body", "title", "img", "nav" }) do
  BLOCK[t] = true
end

local function html_to_text(s)
  s = s:gsub("<!%-%-.-%-%->", " ")
  s = s:gsub("<[hH][eE][aA][dD][%s>].-</[hH][eE][aA][dD]>", " ")
  s = s:gsub("<script.-</script>", " "):gsub("<style.-</style>", " ")
  s = s:gsub("<[!?][^>]*>", " ")
  s = s:gsub("<(/?)([%w:]+)[^>]*>", function(_, name)
    name = name:lower():gsub("^.*:", "")
    if name == "br" then return "\n" end
    return BLOCK[name] and "\n\n" or ""
  end)
  s = s:gsub("&#[xX](%x+);", function(h) return vim.fn.nr2char(tonumber(h, 16)) end)
  s = s:gsub("&#(%d+);", function(d) return vim.fn.nr2char(tonumber(d)) end)
  s = s:gsub("&(%a+);", function(n) return ENTITIES[n] or ("&" .. n .. ";") end)
  return s
end

local function run(cmd)
  local out = vim.fn.system(cmd)
  if vim.v.shell_error ~= 0 then return nil end
  return out
end

local function url_decode(s)
  return (s:gsub("%%(%x%x)", function(h) return string.char(tonumber(h, 16)) end))
end

-- Join "dir/" and "../x.html" style paths inside the EPUB.
local function join(dir, href)
  local parts = {}
  for p in (dir .. href):gmatch("[^/]+") do
    if p == ".." then table.remove(parts)
    elseif p ~= "." then parts[#parts + 1] = p end
  end
  return table.concat(parts, "/")
end

local function dir_of(p) return p:match("^(.*/)") or "" end

local function one_line(s)
  return (html_to_text(s):gsub("%s+", " "):gsub("^ ", ""):gsub(" $", ""))
end

-- The table of contents: { {file =, frag =, title =}, ... } in order.
local function epub_toc(path, opf_xml, dir)
  local nav, ncx
  for tag in opf_xml:gmatch("<[%w:]*item%s[^>]*>") do
    local href = tag:match('href%s*=%s*"([^"]+)"')
    if href then
      local props = tag:match('properties%s*=%s*"([^"]*)"') or ""
      if (" " .. props .. " "):find("%snav%s") then nav = join(dir, url_decode(href)) end
      if tag:find("application/x%-dtbncx%+xml") then ncx = join(dir, url_decode(href)) end
    end
  end
  local toc = {}
  local function add(base, href, label)
    local file, frag = href:match("^([^#]*)#?(.*)$")
    local title = one_line(label)
    if file ~= "" and title ~= "" then
      toc[#toc + 1] = { file = join(dir_of(base), url_decode(file)),
                        frag = frag ~= "" and frag or nil, title = title }
    end
  end
  local xml = nav and run({ "unzip", "-p", path, nav })
  if xml then
    local part = xml:match("<nav[^>]-toc.-</nav>") or xml
    for href, label in part:gmatch('<a%s[^>]-href%s*=%s*"([^"]*)"[^>]*>(.-)</a>') do
      add(nav, href, label)
    end
  end
  if #toc == 0 and ncx then
    xml = run({ "unzip", "-p", path, ncx })
    for label, src in (xml or ""):gmatch(
        "<navLabel>%s*<text>(.-)</text>%s*</navLabel>%s*<content[^>]-src%s*=%s*\"([^\"]+)\"") do
      add(ncx, src, label)
    end
  end
  return toc
end

-- The book in reading order: { {file =, html =, text =}, ... }, and the
-- table of contents.
local function epub_parts(path)
  if vim.fn.executable("unzip") == 0 then
    return nil, "unzip is not installed"
  end
  local container = run({ "unzip", "-p", path, "META-INF/container.xml" })
  local opf = container and container:match('full%-path%s*=%s*"([^"]+)"')
  local opf_xml = opf and run({ "unzip", "-p", path, opf })
  if not opf_xml then return nil, "could not read the EPUB's contents list" end
  local dir = dir_of(opf)

  local href_of = {}
  for tag in opf_xml:gmatch("<[%w:]*item%s[^>]*>") do
    local id, href = tag:match('%sid%s*=%s*"([^"]+)"'), tag:match('href%s*=%s*"([^"]+)"')
    if id and href then href_of[id] = url_decode(href:gsub("#.*$", "")) end
  end
  local parts = {}
  for tag in opf_xml:gmatch("<[%w:]*itemref%s[^>]*>") do
    local href = href_of[tag:match('idref%s*=%s*"([^"]+)"') or ""]
    if href then
      local file = join(dir, href)
      local html = run({ "unzip", "-p", path, file })
      if html then
        parts[#parts + 1] = { file = file, html = html, text = html_to_text(html) }
      end
    end
  end
  if #parts == 0 then return nil, "found no chapters in the EPUB" end
  return parts, epub_toc(path, opf_xml, dir)
end

local function pdf_parts(path)
  if vim.fn.executable("pdftotext") == 0 then
    return nil, "pdftotext (poppler) is not installed"
  end
  local out = run({ "pdftotext", "-enc", "UTF-8", path, "-" })
  if not out then return nil, "pdftotext could not read the PDF" end
  -- Sioyek joins words broken with a hyphen at the end of a line.
  out = out:gsub("([%w\128-\255])%-[ \t]*\n[ \t]*([%w\128-\255])", "%1%2")
  local parts = {}
  for page in (out .. "\f"):gmatch("(.-)\f") do parts[#parts + 1] = { text = page } end
  return parts, nil
end

local function preview(words, b, n)
  local out = {}
  for k = b, math.min(#words, b + n - 1) do out[#out + 1] = words[k].raw end
  return table.concat(out, " ")
end

local books = {}   -- path -> { words =, chapters = }
local function load_book(path)
  if books[path] then return books[path] end
  local is_pdf = path:lower():match("%.pdf$") ~= nil
  local parts, toc
  if is_pdf then parts, toc = pdf_parts(path) else parts, toc = epub_parts(path) end
  if not parts then return nil, toc end

  local words, by_file = {}, {}
  for _, p in ipairs(parts) do
    local ws = words_of((p.text:gsub("\194\173", "")))   -- soft hyphens
    if ws[1] then ws[1].para = true end
    p.first, p.count = #words + 1, #ws
    for _, w in ipairs(ws) do words[#words + 1] = w end
    if p.file and not by_file[p.file] then by_file[p.file] = p end
  end
  if #words == 0 then return nil, "found no text in the book" end

  -- Chapters: from the table of contents; a link to a place inside a
  -- file (file.html#id) starts at the words before that id.
  local chapters, seen = {}, {}
  for _, e in ipairs(toc or {}) do
    local p = by_file[e.file]
    if p and p.count > 0 then
      local b = p.first
      if e.frag then
        local at = p.html:find('id%s*=%s*["\']' .. vim.pesc(e.frag) .. '["\']')
        local tag = at and p.html:sub(1, at):match(".*()<")
        if tag then
          b = p.first + #words_of((html_to_text(p.html:sub(1, tag - 1)):gsub("\194\173", "")))
        end
      end
      if b <= p.first + p.count - 1 and not seen[b] then
        seen[b] = true
        chapters[#chapters + 1] = { b = b, title = e.title }
      end
    end
  end
  if #chapters == 0 then
    for i, p in ipairs(parts) do
      if p.count > 0 then
        chapters[#chapters + 1] = { b = p.first, title = (is_pdf and "Page " or "Part ") .. i }
      end
    end
  end
  table.sort(chapters, function(x, y) return x.b < y.b end)
  for _, c in ipairs(chapters) do
    c.label = c.title .. "  |  " .. preview(words, c.b, 8)
  end
  for _, p in ipairs(parts) do p.html, p.text = nil, nil end
  books[path] = { words = words, chapters = chapters }
  return books[path]
end

---------------------------------------------------------------------------
-- Lining up what you typed with the book
---------------------------------------------------------------------------

-- Where in the book (at or after `from`) do the first typed words
-- appear? Skips up to 10 typed words (e.g. a heading you added).
local function find_start(book, typed, from, t_from)
  t_from = t_from or 1
  local want = math.min(5, #typed - t_from + 1)
  if want < 3 then return nil end
  for skip = 0, math.min(10, #typed - t_from + 1 - want) do
    for b = from or 1, #book - want + 1 do
      local ok = true
      for k = 1, want do
        if book[b + k - 1].key ~= typed[t_from - 1 + skip + k].key then ok = false break end
      end
      if ok then return b, t_from + skip end
    end
  end
end

-- Lines up typed words t.. with book words b... Words up to `done` are
-- finished; a word after that is still being typed and simply goes with
-- the next book word.
-- Returns:
--   match[t]  book index for typed word t, or false (not in the book)
--   missed    { {t =, b =}, ... }: book word b was skipped, before typed t
--   cur_b     book word for the word being typed (nil if none)
--   next_b    the book word that comes next
local function align(book, typed, b, t, done)
  local match, missed = {}, {}
  local function eq(ti, bi)
    return ti <= done and typed[ti] and book[bi] and typed[ti].key == book[bi].key
  end
  -- How many of the next three typed words agree with the book?
  local function agree(bi, ti)
    local c = 0
    for k = 0, 2 do if eq(ti + k, bi + k) then c = c + 1 end end
    return c
  end
  -- How well does typed word ti fit book word bi on its own?
  local function fit(ti, bi)
    if not book[bi] then return nil end
    if eq(ti, bi) then return 2 end
    if close(typed[ti], book[bi]) then return 1.5 end
  end

  while t <= done and b <= #book do
    if eq(t, b) then
      match[t] = b
      b, t = b + 1, t + 1
    else
      -- A typo (or a wrong word) in this word...
      local best, kind, n = (fit(t, b) or 0.5) + agree(b + 1, t + 1), "typo", 0
      -- ...or book words skipped (more than 8 only if 2 more words agree)...
      for k = 1, 40 do
        local s = fit(t, b + k)
        if s then
          local a = agree(b + k + 1, t + 1)
          if k <= 8 or a >= 2 then
            s = s - 0.6 - 0.05 * k + a
            if s > best then best, kind, n = s, "skip", k end
          end
        end
      end
      -- ...or typed words that are not in the book.
      for j = 1, 3 do
        local s = t + j <= done and fit(t + j, b)
        if s then
          s = s - 0.6 - 0.25 * (j - 1) + agree(b + 1, t + j + 1)
          if s > best then best, kind, n = s, "extra", j end
        end
      end
      if kind == "typo" then
        match[t] = b
        b, t = b + 1, t + 1
      elseif kind == "skip" then
        for q = b, b + n - 1 do missed[#missed + 1] = { t = t, b = q } end
        b = b + n
      else
        for q = t, t + n - 1 do match[q] = false end
        t = t + n
      end
    end
  end
  for q = t, done do match[q] = false end        -- past the end of the book
  local cur_b
  if done < #typed then
    cur_b = book[b] and b or nil
    match[#typed] = cur_b or false
    if cur_b then b = b + 1 end
  end
  return match, missed, cur_b, b
end

---------------------------------------------------------------------------
-- Talking to Sioyek (only with opts.sioyek_marks)
---------------------------------------------------------------------------

local queue, busy = {}, false

-- Is Sioyek open? Without it, `sioyek --execute-command` would start a
-- new Sioyek every time (it looked like Sioyek reopening by itself).
local function sioyek_running()
  vim.fn.system({ "pgrep", "-x", vim.fn.fnamemodify(M.opts.sioyek, ":t") })
  return vim.v.shell_error == 0
end

local warned_closed = false
local function pump()
  if busy or #queue == 0 then return end
  if not sioyek_running() then
    queue = {}
    if not warned_closed then
      warned_closed = true
      vim.notify("TypeTrack: Sioyek is not open; open the book in it to see the marks",
        vim.log.levels.WARN)
    end
    return
  end
  warned_closed = false
  busy = true
  local job = table.remove(queue, 1)
  -- One command at a time. Each is a macro that Sioyek runs in one go,
  -- with no waiting step in it: wait_for_search_to_finish, together with
  -- --wait-for-response or a second macro arriving while it waited,
  -- crashed Sioyek ("Memory fault", 2026-11-12).
  local id = vim.fn.jobstart(job.cmd, {
    on_exit = function()
      vim.defer_fn(function() busy = false pump() end, job.wait or 0)
    end,
  })
  if id <= 0 then
    busy = false
    queue = {}
    vim.notify("TypeTrack: could not run " .. M.opts.sioyek, vim.log.levels.ERROR)
  end
end

local function sioyek(macro, wait)
  queue[#queue + 1] = {
    cmd = { M.opts.sioyek, "--nofocus", "--execute-command", macro },
    wait = wait,
  }
  pump()
end

-- A search pattern (ECMAScript regex, as Sioyek uses) for book words
-- first..last, followed by enough of the next words to be the only
-- such place in the book. A drop cap may be split from its word.
local function word_rx(w)
  local parts = { unpack(w.parts) }
  local first = parts[1]
  if first:match("^%w") and #first > 1 then
    parts[1] = first:sub(1, 1) .. "\\s*" .. first:sub(2)
  end
  return table.concat(parts, "\\W*")
end

local function count_from(book, first, n)
  local c = 0
  for b = 1, #book - n + 1 do
    local ok = true
    for k = 0, n - 1 do
      if book[b + k].key ~= book[first + k].key then ok = false break end
    end
    if ok then c = c + 1 if c > 1 then return c end end
  end
  return c
end

local function pattern(book, first, last)
  local rx = {}
  for b = first, last do rx[#rx + 1] = word_rx(book[b]) end
  local p = table.concat(rx, "\\W+")
  if book[first].parts[1]:match("^%w") then p = "\\b" .. p end
  local n = last - first + 1
  local ctx = {}
  while last + #ctx < #book and #ctx < 12 do
    if #ctx >= 3 and count_from(book, first, n + #ctx) <= 1 then break end
    ctx[#ctx + 1] = word_rx(book[last + #ctx + 1])
  end
  if #ctx > 0 then
    p = p .. "(?=\\W+" .. table.concat(ctx, "\\W+") .. ")"
  elseif book[last].parts[#book[last].parts]:match("%w$") then
    p = p .. "\\b"
  end
  return p
end

-- Inside a quoted Sioyek command argument a backslash escapes the next
-- character, so every backslash is doubled.
local function quote(s)
  return "'" .. s:gsub("\\", "\\\\"):gsub("'", "\\'"):gsub(",", "\\,") .. "'"
end

local function highlight(book, first, last, good)
  local kind = good and M.opts.good or M.opts.bad
  sioyek("regex_search(" .. quote(pattern(book, first, last)) ..
    ");next_item;select_current_search_match;add_highlight(" .. kind .. ")", 50)
end

---------------------------------------------------------------------------
-- Remembering (book, mode, where you started) per text file
---------------------------------------------------------------------------

local state_file = vim.fn.stdpath("state") .. "/typetrack.json"
local function load_state()
  local f = io.open(state_file, "r")
  if not f then return {} end
  local ok, data = pcall(vim.json.decode, f:read("*a"))
  f:close()
  return ok and type(data) == "table" and data or {}
end
local function save_state(data)
  vim.fn.mkdir(vim.fn.fnamemodify(state_file, ":h"), "p")
  local f = io.open(state_file, "w")
  if f then f:write(vim.json.encode(data)) f:close() end
end

local tracked = {}   -- buf -> { book_path, book, chapters, ghost, start, upto, ... }

local function remember(buf)
  local tr = tracked[buf]
  local name = vim.api.nvim_buf_get_name(buf)
  local data = load_state()
  data[name] = { book = tr.book_path, upto = tr.upto,
                 mode = tr.ghost and "ghost" or "check", start = tr.start }
  save_state(data)
end

---------------------------------------------------------------------------
-- Showing it
---------------------------------------------------------------------------

local function mark(buf, lnum, col, ecol, hl)
  vim.api.nvim_buf_set_extmark(buf, ns, lnum, col, { end_col = ecol, hl_group = hl })
end

-- Book text shown in place (inline) at line/col; on Neovim 0.9 at the
-- end of the line instead.
local function virtual(buf, lnum, col, text, hl)
  vim.api.nvim_buf_set_extmark(buf, ns, lnum, INLINE and col or 0, {
    virt_text = { { text, hl } }, virt_text_pos = INLINE and "inline" or "eol",
    right_gravity = false,
  })
end

local function show_word(buf, w, bw, in_progress)
  if not bw then                                   -- not in the book
    return mark(buf, w.lnum, w.col, w.ecol, "TypeTrackBad")
  end
  if same(w, bw) then return end
  local bad, missing
  if in_progress then
    bad, missing = prefix_diff(w.raw, bw.raw), {}
  else
    bad, missing = char_diff(w.raw, bw.raw)
  end
  local cs = chars(w.raw)
  for i, ch in ipairs(cs) do
    if bad[i] then
      mark(buf, w.lnum, w.col + ch.at, w.col + ch.at + #ch.c, "TypeTrackBad")
    end
  end
  for i, text in pairs(missing) do
    local at = cs[i] and cs[i].at or #w.raw
    virtual(buf, w.lnum, w.col + at, text, "TypeTrackMissing")
  end
end

-- Ghost text: the rest of the word being typed, then the next book
-- words, laid out like the book (paragraphs) and wrapped at the width.
local function show_ghost(buf, tr, typed, cur_b, next_b)
  local book = tr.book
  local last = vim.api.nvim_buf_line_count(buf) - 1
  local line = vim.api.nvim_buf_get_lines(buf, last, last + 1, false)[1] or ""
  local width = M.opts.width
  if not width then
    local tw = vim.bo[buf].textwidth
    width = tw > 0 and tw or 80
  end
  local dw = vim.fn.strdisplaywidth
  local rows = { { text = "", base = dw(line) } }
  if cur_b then
    local typed_n = vim.fn.strchars(typed[#typed].raw)
    rows[1].text = vim.fn.strcharpart(book[cur_b].raw, typed_n)
  end
  local function used(r) return r.base + dw(r.text) end
  local b = next_b
  while book[b] do
    local w, r = book[b], rows[#rows]
    local gap = w.gap
    local at_start = used(r) == 0 or (r.text == "" and line:match("%s$"))
    if w.para and used(r) > 0 then
      rows[#rows + 1] = { text = "", base = 0 }    -- blank line, then
      rows[#rows + 1] = { text = "", base = 0 }    -- the new paragraph
      gap = ""
    elseif at_start then
      gap = gap:gsub("^%s+", "")
    elseif used(r) + dw(gap .. w.raw) > width then
      rows[#rows + 1] = { text = "", base = 0 }
      gap = gap:gsub("^%s+", "")
    end
    if #rows > M.opts.ghost_lines + 1 then break end
    r = rows[#rows]
    r.text = r.text .. gap .. w.raw
    b = b + 1
  end
  while #rows > M.opts.ghost_lines + 1 do table.remove(rows) end
  -- right_gravity = true: Neovim draws inline text of such a mark after
  -- the cursor standing on its column (with false the cursor ended up
  -- after the ghost text, reported 2026-11-14).
  local opts = { right_gravity = true }
  if rows[1].text ~= "" then
    opts.virt_text = { { rows[1].text, "TypeTrackGhost" } }
    opts.virt_text_pos = INLINE and "inline" or "eol"
  end
  if #rows > 1 then
    opts.virt_lines = {}
    for k = 2, #rows do
      opts.virt_lines[#opts.virt_lines + 1] = { { rows[k].text, "TypeTrackGhost" } }
    end
  end
  if opts.virt_text or opts.virt_lines then
    vim.api.nvim_buf_set_extmark(buf, ns, last, INLINE and #line or 0, opts)
  end
end

---------------------------------------------------------------------------
-- Following a buffer
---------------------------------------------------------------------------

local function process(buf, flush)
  local tr = tracked[buf]
  if not tr or not vim.api.nvim_buf_is_valid(buf) then return end
  local typed = buffer_words(buf)
  local book = tr.book

  if not tr.start then
    local b0, t0 = find_start(book, typed, 1)
    if b0 then
      tr.start = { b = b0, t = t0 }
      remember(buf)
    end
  end
  vim.api.nvim_buf_clear_namespace(buf, ns, 0, -1)
  local s = tr.start
  if not s then return end
  local t0 = math.min(s.t, #typed + 1)

  -- The last word is still being typed while nothing comes after it.
  -- In ghost mode it stays that way (its ghost shows the rest of it);
  -- in check mode a pause or leaving insert mode finishes it.
  local done = #typed
  local lastw = typed[#typed]
  if lastw and lastw.ecol and (tr.ghost or not flush) then
    local count = vim.api.nvim_buf_line_count(buf)
    local line = vim.api.nvim_buf_get_lines(buf, count - 1, count, false)[1] or ""
    if lastw.lnum == count - 1 and #line == lastw.ecol then done = #typed - 1 end
  end
  if done < t0 - 1 then done = t0 - 1 end

  local match, missed, cur_b, next_b = align(book, typed, s.b, t0, done)

  for t = t0, #typed do
    local bi = match[t]
    show_word(buf, typed[t], bi and book[bi] or nil, t > done)
  end
  -- Skipped book words, crossed out, where they should have been.
  local k = 1
  while missed[k] do
    local t, words = missed[k].t, {}
    while missed[k] and missed[k].t == t do
      words[#words + 1] = book[missed[k].b].raw
      k = k + 1
    end
    virtual(buf, typed[t].lnum, typed[t].col, table.concat(words, " ") .. " ",
      "TypeTrackMissing")
  end
  if tr.ghost then show_ghost(buf, tr, typed, cur_b, next_b) end

  if not M.opts.sioyek_marks then return end

  -- Sioyek: book words in order, right or wrong (finished words only).
  local marks = {}
  for t = t0, done do
    if match[t] then marks[#marks + 1] = { b = match[t], good = same(typed[t], book[match[t]]) } end
  end
  for _, m in ipairs(missed) do marks[#marks + 1] = { b = m.b, good = false } end
  table.sort(marks, function(x, y) return x.b < y.b end)

  -- On a first start, text that was already in the file is not sent
  -- (unless started with :TypeTrack!), only what you type from now on.
  if tr.skip_existing then
    tr.skip_existing = false
    if #marks > 0 then tr.upto = math.max(tr.upto, marks[#marks].b) end
  end
  local before = tr.upto
  local i = 1
  while i <= #marks do
    local m = marks[i]
    if m.b > tr.upto then
      local j = i
      while marks[j + 1] and marks[j + 1].good == m.good
          and marks[j + 1].b == marks[j].b + 1
          and marks[j + 1].b - m.b < M.opts.max_run do
        j = j + 1
      end
      if j == #marks and not flush
          and marks[j].b - m.b + 1 < M.opts.max_run then
        break
      end
      highlight(book, m.b, marks[j].b, m.good)
      tr.upto = marks[j].b
      i = j + 1
    else
      i = i + 1
    end
  end
  if tr.upto ~= before then remember(buf) end
end

function M.stop(buf, quiet)
  buf = buf or vim.api.nvim_get_current_buf()
  local tr = tracked[buf]
  if not tr then return end
  tr.pause:stop() tr.pause:close()
  pcall(vim.api.nvim_del_augroup_by_id, tr.group)
  vim.api.nvim_buf_clear_namespace(buf, ns, 0, -1)
  tracked[buf] = nil
  if not quiet then vim.notify("TypeTrack stopped") end
end

-- Start typing at book word b (nil: find the place from what is typed).
local function set_start(buf, b)
  local tr = tracked[buf]
  if not tr then return end
  if not b then
    tr.start = nil
  else
    local typed = buffer_words(buf)
    if #typed == 0 then
      tr.start = { b = b, t = 1 }
    else
      -- Already typed some of it? Then line up from there; otherwise
      -- what you type from now on starts at b.
      local fb, ft = find_start(tr.book, typed, b)
      if fb and fb < b + 2000 then
        tr.start = { b = fb, t = ft }
      else
        tr.start = { b = b, t = #typed + 1 }
      end
    end
  end
  remember(buf)
  process(buf, false)
end

function M.choose_start(buf)
  buf = buf or vim.api.nvim_get_current_buf()
  local tr = tracked[buf]
  if not tr then
    return vim.notify("TypeTrack: start it first (:TypeGhost or :TypeTrack)",
      vim.log.levels.WARN)
  end
  local items = {}
  if not tr.ghost then
    items[1] = { label = "Find the place from what I type" }
  end
  for _, c in ipairs(tr.chapters) do items[#items + 1] = c end
  vim.ui.select(items, {
    prompt = "Start typing at",
    format_item = function(c) return c.label end,
  }, function(c)
    if c then set_start(buf, c.b) end
  end)
end

-- mode: "ghost" or "check" (nil: the mode last used with this file).
function M.start(book_path, all, mode)
  local buf = vim.api.nvim_get_current_buf()
  local name = vim.api.nvim_buf_get_name(buf)
  if name == "" then
    return vim.notify("TypeTrack: save the file first", vim.log.levels.WARN)
  end
  local saved = load_state()[name] or {}
  if book_path == nil or book_path == "" then book_path = saved.book end
  if not book_path then
    return vim.notify("TypeTrack: which book? :TypeGhost path/to/book.epub",
      vim.log.levels.WARN)
  end
  book_path = vim.fn.fnamemodify(vim.fn.expand(book_path), ":p")
  mode = mode or saved.mode or "check"
  if M.opts.sioyek_marks and vim.fn.executable(M.opts.sioyek) == 0 then
    return vim.notify("TypeTrack: sioyek not found", vim.log.levels.ERROR)
  end
  local loaded, err = load_book(book_path)
  if not loaded then
    return vim.notify("TypeTrack: " .. err, vim.log.levels.ERROR)
  end
  if tracked[buf] then M.stop(buf, true) end

  -- A different book starts from scratch; the same book carries on.
  local same_book = saved.book == book_path
  local upto = (same_book and saved.upto) or 0
  local tr = { book_path = book_path, book = loaded.words, chapters = loaded.chapters,
               ghost = mode == "ghost", upto = upto,
               start = same_book and type(saved.start) == "table" and saved.start or nil,
               pause = uv.new_timer(),
               skip_existing = upto == 0 and not all and #buffer_words(buf) > 3 }
  tracked[buf] = tr
  remember(buf)

  tr.group = vim.api.nvim_create_augroup("TypeTrack" .. buf, { clear = true })
  vim.api.nvim_create_autocmd({ "TextChanged", "TextChangedI" }, {
    group = tr.group, buffer = buf,
    callback = function()
      process(buf, false)
      tr.pause:stop()
      tr.pause:start(M.opts.pause_ms, 0, vim.schedule_wrap(function() process(buf, true) end))
    end,
  })
  vim.api.nvim_create_autocmd("InsertLeave", {
    group = tr.group, buffer = buf,
    callback = function() process(buf, true) end,
  })
  vim.api.nvim_create_autocmd("BufWipeout", {
    group = tr.group, buffer = buf,
    callback = function() M.stop(buf, true) end,
  })

  vim.notify(("TypeTrack: %s mode, %s (%d words)"):format(
    tr.ghost and "ghost" or "check", vim.fn.fnamemodify(book_path, ":t"), #tr.book))
  process(buf, true)
  -- No place in the book yet: ask where to start.
  if not tr.start then M.choose_start(buf) end
end

function M.is_on(buf)
  return tracked[buf or vim.api.nvim_get_current_buf()] ~= nil
end

function M.mode(buf)
  local tr = tracked[buf or vim.api.nvim_get_current_buf()]
  return tr and (tr.ghost and "ghost" or "check") or nil
end

-- For testing: the pieces above.
M._internal = { words_of = words_of, load_book = load_book, align = align,
  find_start = find_start, pattern = pattern, quote = quote,
  char_diff = char_diff, prefix_diff = prefix_diff, set_start = set_start,
  buffer_words = buffer_words, process = process, tracked = tracked, ns = ns,
  books = books }

return M
