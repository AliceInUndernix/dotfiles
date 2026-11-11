-- ~/.config/nvim/lua/typetrack.lua
-- Type a book out in Neovim while it is open in Sioyek, and Sioyek marks
-- each word of the book: green when you typed it right, red when not.
--
--   :TypeTrack ~/books/book.epub   start for the file you are in
--   :TypeTrack                     start again (remembers the book)
--   :TypeTrack! ...                also mark what the file already has
--   :TypeTrackStop                 stop
--
-- Needs a Sioyek that has the select_current_search_match command
-- (the development version, 3.x / Gentoo's sioyek-9999), plus `unzip`
-- for EPUB or `pdftotext` (poppler) for PDF. Release 2.0.0 and the
-- sioyek3-alpha0 preview do not have that command.
--
-- How it works:
--   1. The book's text is read by Neovim itself (EPUB: unzip the chapters
--      in spine order and strip the HTML; PDF: pdftotext) and split into
--      words.
--   2. On every change, the buffer's words are lined up with the book:
--      find_start() locates the first typed words in the book, align()
--      walks along word by word, allowing skipped book words and extra
--      typed words. The last M.opts.lag words wait (steno corrections).
--   3. Runs of same-coloured book words are sent to Sioyek with
--      `sioyek --nofocus --execute-command`: a regex search unique enough
--      to hit only those words, then select_current_search_match and
--      add_highlight(g or r). Commands are queued, one at a time.
--   4. Wrong typed words get an underline in Neovim (extmarks).
--   5. Book path and how far it has marked are saved per text file in
--      stdpath("state")/typetrack.json, so it carries on next time.
--
-- Testing status: the Neovim side (steps 1, 2, 4) was run by its author
-- on a sample EPUB (typos, skipped and extra words judged right), and the
-- search patterns were checked with an ECMAScript regex engine like
-- Sioyek's. Step 3 has NEVER run against a real Sioyek: the user had
-- 2.0.0 and was installing sioyek-9999 from GURU (blocked on a Qt
-- 6.11.2/6.11.3 slot conflict; suggested fix was an @world update).
-- Whether it works in Sioyek is unknown.
--
-- M._internal at the bottom exposes the pieces for headless tests.

local M = {}
local uv = vim.uv or vim.loop

M.opts = {
  sioyek = "sioyek",     -- the sioyek program
  good = "g",            -- Sioyek highlight type for right words (green)
  bad = "r",             -- ...and for wrong or skipped words (red)
  strict = true,         -- capitals and punctuation must match too
  lag = 2,               -- wait this many words before judging a word,
                         -- so a quick steno correction is not marked red
  pause_ms = 1500,       -- after this long without typing, judge them all
  max_run = 10,          -- longest single highlight, in words
}

local ns = vim.api.nvim_create_namespace("typetrack")
vim.api.nvim_set_hl(0, "TypeTrackBad", { default = true, link = "DiagnosticUnderlineError" })

---------------------------------------------------------------------------
-- Words
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

local function words_of(text)
  local list = {}
  for tok in blank_dashes(text):gmatch("%S+") do
    local w = make_word(tok)
    if w then list[#list + 1] = w end
  end
  return list
end

-- Words of the buffer, with where each one sits (for underlining).
local function buffer_words(buf)
  local list = {}
  for lnum, line in ipairs(vim.api.nvim_buf_get_lines(buf, 0, -1, false)) do
    for s, tok, e in blank_dashes(line):gmatch("()(%S+)()") do
      local w = make_word(line:sub(s, e - 1))
      if w then
        w.lnum, w.col, w.ecol = lnum - 1, s - 1, e - 1
        list[#list + 1] = w
      end
    end
  end
  return list
end

---------------------------------------------------------------------------
-- Reading the book
---------------------------------------------------------------------------

local ENTITIES = {
  amp = "&", lt = "<", gt = ">", quot = '"', apos = "'", nbsp = " ",
  mdash = "—", ndash = "–", hellip = "…", lsquo = "‘", rsquo = "’",
  ldquo = "“", rdquo = "”", shy = "",
}
local BLOCK = {}
for _, t in ipairs({ "p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6",
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
    return BLOCK[name:lower():gsub("^.*:", "")] and " " or ""
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

local function epub_text(path)
  if vim.fn.executable("unzip") == 0 then
    return nil, "unzip is not installed"
  end
  local container = run({ "unzip", "-p", path, "META-INF/container.xml" })
  local opf = container and container:match('full%-path%s*=%s*"([^"]+)"')
  local opf_xml = opf and run({ "unzip", "-p", path, opf })
  if not opf_xml then return nil, "could not read the EPUB's contents list" end
  local dir = opf:match("^(.*/)") or ""

  local href_of = {}
  for tag in opf_xml:gmatch("<[%w:]*item%s[^>]*>") do
    local id, href = tag:match('%sid%s*=%s*"([^"]+)"'), tag:match('href%s*=%s*"([^"]+)"')
    if id and href then href_of[id] = url_decode(href:gsub("#.*$", "")) end
  end
  local chunks = {}
  for tag in opf_xml:gmatch("<[%w:]*itemref%s[^>]*>") do
    local href = href_of[tag:match('idref%s*=%s*"([^"]+)"') or ""]
    if href then
      local html = run({ "unzip", "-p", path, join(dir, href) })
      if html then chunks[#chunks + 1] = html_to_text(html) end
    end
  end
  if #chunks == 0 then return nil, "found no chapters in the EPUB" end
  return table.concat(chunks, "\n")
end

local function pdf_text(path)
  if vim.fn.executable("pdftotext") == 0 then
    return nil, "pdftotext (poppler) is not installed"
  end
  local out = run({ "pdftotext", "-enc", "UTF-8", path, "-" })
  if not out then return nil, "pdftotext could not read the PDF" end
  -- Sioyek joins words broken with a hyphen at the end of a line.
  return (out:gsub("([%w\128-\255])%-%s*\n%s*([%w\128-\255])", "%1%2"))
end

local books = {}   -- path -> list of words
local function load_book(path)
  if books[path] then return books[path] end
  local text, err
  if path:lower():match("%.pdf$") then text, err = pdf_text(path)
  else text, err = epub_text(path) end
  if not text then return nil, err end
  text = text:gsub("\194\173", "")   -- soft hyphens
  books[path] = words_of(text)
  return books[path]
end

---------------------------------------------------------------------------
-- Lining up what you typed with the book
---------------------------------------------------------------------------

local function same(a, b)
  if M.opts.strict then return a.norm == b.norm end
  return a.key == b.key
end

-- Where in the book does the typing start? Look for the first few typed
-- words (skipping up to 10 words, e.g. a heading you added yourself).
local function find_start(book, typed)
  local want = math.min(5, #typed)
  if want < 3 then return nil end
  for skip = 0, math.min(10, #typed - want) do
    for b = 1, #book - want + 1 do
      local ok = true
      for k = 1, want do
        if book[b + k - 1].key ~= typed[skip + k].key then ok = false break end
      end
      if ok then return b, skip + 1 end
    end
  end
end

-- Returns marks = { {b = book index, good = bool}, ... } in book order,
-- and the list of typed words that are wrong.
local function align(book, typed, b, t, limit)
  local marks, wrong = {}, {}
  while t <= limit and b <= #book do
    local w = typed[t]
    if w.key == book[b].key then
      local good = same(w, book[b])
      marks[#marks + 1] = { b = b, good = good }
      if not good then wrong[#wrong + 1] = w end
      b, t = b + 1, t + 1
    else
      -- Skipped some book words? (the next typed word must agree too)
      local resync
      for q = b + 1, math.min(b + 4, #book) do
        if book[q].key == w.key and (t == limit or
            (book[q + 1] and typed[t + 1].key == book[q + 1].key)) then
          resync = q
          break
        end
      end
      if resync then
        for q = b, resync - 1 do marks[#marks + 1] = { b = q, good = false } end
        b = resync
      elseif typed[t + 1] and t < limit and typed[t + 1].key == book[b].key then
        wrong[#wrong + 1] = w           -- an extra word that is not in the book
        t = t + 1
      else
        marks[#marks + 1] = { b = b, good = false }   -- a wrong word
        wrong[#wrong + 1] = w
        b, t = b + 1, t + 1
      end
    end
  end
  return marks, wrong
end

---------------------------------------------------------------------------
-- Talking to Sioyek
---------------------------------------------------------------------------

local queue, busy = {}, false

local function pump()
  if busy or #queue == 0 then return end
  busy = true
  local job = table.remove(queue, 1)
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
-- such place in the book. Only the words themselves get highlighted.
local function word_rx(w) return table.concat(w.parts, "\\W*") end

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
  -- 1) search; Sioyek scrolls to the match
  sioyek("regex_search(" .. quote(pattern(book, first, last)) ..
    ");wait_for_search_to_finish;next_item", 150)
  -- 2) select that match and highlight it (a separate step, after
  --    Sioyek has drawn the match)
  sioyek("select_current_search_match;add_highlight(" .. kind .. ")")
end

---------------------------------------------------------------------------
-- Remembering progress (which book, how far marked) per text file
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
  local f = io.open(state_file, "w")
  if f then f:write(vim.json.encode(data)) f:close() end
end

---------------------------------------------------------------------------
-- Following a buffer
---------------------------------------------------------------------------

local tracked = {}   -- buf -> { book_path, book, upto, timer, group }

local function process(buf, flush)
  local tr = tracked[buf]
  if not tr or not vim.api.nvim_buf_is_valid(buf) then return end
  local typed = buffer_words(buf)
  -- Where the typing starts in the book (kept while those words stay).
  local s = tr.start
  if s then
    for k = 0, 4 do
      local w, bw = typed[s.t + k], tr.book[s.b + k]
      if not w or not bw or w.key ~= bw.key then s = nil break end
    end
  end
  if not s then
    local b0, t0 = find_start(tr.book, typed)
    s = b0 and { b = b0, t = t0 } or nil
    tr.start = s
  end
  vim.api.nvim_buf_clear_namespace(buf, ns, 0, -1)
  if not s then return end
  local limit = flush and #typed or (#typed - M.opts.lag)
  local marks, wrong = align(tr.book, typed, s.b, s.t, limit)

  -- On a first start, text that was already in the file is not sent
  -- (unless started with :TypeTrack!), only what you type from now on.
  if tr.skip_existing then
    tr.skip_existing = false
    if #marks > 0 then tr.upto = math.max(tr.upto, marks[#marks].b) end
  end
  local before = tr.upto

  for _, w in ipairs(wrong) do
    vim.api.nvim_buf_set_extmark(buf, ns, w.lnum, w.col,
      { end_col = w.ecol, hl_group = "TypeTrackBad" })
  end

  -- Send the marks Sioyek has not had yet, joined into runs of the same
  -- colour (one highlight per run).
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
      -- While you type, the last run waits until it is full or the
      -- colour changes, so Sioyek gets a few long highlights instead of
      -- one per word. A pause sends it.
      if j == #marks and not flush
          and marks[j].b - m.b + 1 < M.opts.max_run then
        break
      end
      highlight(tr.book, m.b, marks[j].b, m.good)
      tr.upto = marks[j].b
      i = j + 1
    else
      i = i + 1
    end
  end

  if tr.upto == before then return end
  local name = vim.api.nvim_buf_get_name(buf)
  local data = load_state()
  data[name] = { book = tr.book_path, upto = tr.upto }
  save_state(data)
end

function M.stop(buf)
  buf = buf or vim.api.nvim_get_current_buf()
  local tr = tracked[buf]
  if not tr then return end
  tr.timer:stop() tr.timer:close()
  tr.pause:stop() tr.pause:close()
  pcall(vim.api.nvim_del_augroup_by_id, tr.group)
  vim.api.nvim_buf_clear_namespace(buf, ns, 0, -1)
  tracked[buf] = nil
  vim.notify("TypeTrack stopped")
end

function M.start(book_path, all)
  local buf = vim.api.nvim_get_current_buf()
  local name = vim.api.nvim_buf_get_name(buf)
  if name == "" then
    return vim.notify("TypeTrack: save the file first", vim.log.levels.WARN)
  end
  local saved = load_state()[name] or {}
  if book_path == nil or book_path == "" then book_path = saved.book end
  if not book_path then
    return vim.notify("TypeTrack: which book? :TypeTrack path/to/book.epub",
      vim.log.levels.WARN)
  end
  book_path = vim.fn.fnamemodify(vim.fn.expand(book_path), ":p")
  if vim.fn.executable(M.opts.sioyek) == 0 then
    return vim.notify("TypeTrack: sioyek not found", vim.log.levels.ERROR)
  end
  local book, err = load_book(book_path)
  if not book then
    return vim.notify("TypeTrack: " .. err, vim.log.levels.ERROR)
  end
  if tracked[buf] then M.stop(buf) end

  -- A different book starts from scratch; the same book carries on.
  local upto = (saved.book == book_path and saved.upto) or 0
  local tr = { book_path = book_path, book = book, upto = upto,
               timer = uv.new_timer(), pause = uv.new_timer(),
               skip_existing = upto == 0 and not all
                 and #buffer_words(buf) > M.opts.lag + 3 }
  tracked[buf] = tr

  tr.group = vim.api.nvim_create_augroup("TypeTrack" .. buf, { clear = true })
  local function later(t, ms, flush)
    t:stop()
    t:start(ms, 0, vim.schedule_wrap(function() process(buf, flush) end))
  end
  vim.api.nvim_create_autocmd({ "TextChanged", "TextChangedI" }, {
    group = tr.group, buffer = buf,
    callback = function()
      later(tr.timer, 250, false)
      later(tr.pause, M.opts.pause_ms, true)
    end,
  })
  vim.api.nvim_create_autocmd("InsertLeave", {
    group = tr.group, buffer = buf,
    callback = function() process(buf, true) end,
  })
  vim.api.nvim_create_autocmd("BufWipeout", {
    group = tr.group, buffer = buf,
    callback = function() M.stop(buf) end,
  })

  vim.notify(("TypeTrack: following %s (%d words)%s"):format(
    vim.fn.fnamemodify(book_path, ":t"), #book,
    upto > 0 and (", carrying on after word " .. upto) or ""))
  process(buf, true)
end

function M.is_on(buf)
  return tracked[buf or vim.api.nvim_get_current_buf()] ~= nil
end

-- For testing: the pieces above.
M._internal = { words_of = words_of, load_book = load_book, align = align,
  find_start = find_start, pattern = pattern, quote = quote,
  buffer_words = buffer_words, process = process, tracked = tracked }

return M
