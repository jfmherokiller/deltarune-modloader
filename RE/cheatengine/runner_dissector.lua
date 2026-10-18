--[[ GameMaker Runner Dissector for Cheat Engine 7.x  (autorun script)

  Like CE's Mono/.NET dissectors, but for GameMaker runner games.
  Install: copy this file into "<Cheat Engine>\autorun\". A "GameMaker" menu appears.

  Two backends, chosen automatically after you attach to a game:
   * injected  - RunnerEditor.dll (Aurie/YYToolkit plugin) serves \\.\pipe\RunnerCE-<pid>.
                 Works for any game YYToolkit loads into (VM or YYC). Read-only pipe:
                 it reports variable names/types and the address of each live RValue;
                 Cheat Engine does all editing/freezing itself.
   * external  - no injection: walks DELTARUNE's runner memory directly (build-specific
                 layout from RE/08_instances.md / deltarune_stats.lua). Globals + raw instances.

  Lua API (console):  gm_backend()  gm_globals()  gm_instances()  gm_members(id)
                      gm_find('gold')  -> address of global.gold's RValue
  RValue layout: +0 value (double/int32/int64/pointer), +12 kind (low 24 bits).
]]

local GM = { version = 1, pageLimit = 400, timeout = 1500 }
GMRunner = GM

------------------------------------------------------------------ JSON (decode only)
local json = {}
do
  local esc = { ['"']='"', ['\\']='\\', ['/']='/', b='\b', f='\f', n='\n', r='\r', t='\t' }
  local function ws(s, i) return s:find('[^ \t\r\n]', i) or #s + 1 end
  local value
  local function str(s, i)
    local out, j = {}, i + 1
    while true do
      local c = s:sub(j, j)
      if c == '' then error('unterminated string') end
      if c == '"' then return table.concat(out), j + 1 end
      if c == '\\' then
        local e = s:sub(j + 1, j + 1)
        if e == 'u' then
          local cp = tonumber(s:sub(j + 2, j + 5), 16) or 63
          out[#out + 1] = utf8.char(cp); j = j + 6
        else out[#out + 1] = esc[e] or e; j = j + 2 end
      else
        local k = s:find('["\\]', j) or #s + 1
        out[#out + 1] = s:sub(j, k - 1); j = k
      end
    end
  end
  value = function(s, i)
    i = ws(s, i)
    local c = s:sub(i, i)
    if c == '{' then
      local t = {}; i = ws(s, i + 1)
      if s:sub(i, i) == '}' then return t, i + 1 end
      while true do
        local k; k, i = str(s, ws(s, i)); i = ws(s, i)
        if s:sub(i, i) ~= ':' then error('expected :') end
        t[k], i = value(s, i + 1); i = ws(s, i)
        local d = s:sub(i, i); i = i + 1
        if d == '}' then return t, i elseif d ~= ',' then error('expected , or }') end
      end
    elseif c == '[' then
      local t = {}; i = ws(s, i + 1)
      if s:sub(i, i) == ']' then return t, i + 1 end
      while true do
        t[#t + 1], i = value(s, i); i = ws(s, i)
        local d = s:sub(i, i); i = i + 1
        if d == ']' then return t, i elseif d ~= ',' then error('expected , or ]') end
      end
    elseif c == '"' then return str(s, i)
    elseif s:sub(i, i + 3) == 'true' then return true, i + 4
    elseif s:sub(i, i + 4) == 'false' then return false, i + 5
    elseif s:sub(i, i + 3) == 'null' then return nil, i + 4
    else
      local num = s:match('^-?%d+%.?%d*[eE]?[-+]?%d*', i)
      if not num or num == '' then error('bad json at ' .. i) end
      return tonumber(num), i + #num
    end
  end
  function json.decode(s) local v = value(s, 1); return v end
end
GM.json = json

------------------------------------------------------------------ helpers
local KIND = { [0]='real', [1]='string', [2]='array', [3]='pointer', [5]='undefined', [6]='struct',
               [7]='int32', [10]='int64', [12]='null', [13]='bool', [15]='ref' }
GM.KIND = KIND
local function hex(a) return a and string.format('%X', a) or '?' end
local function addrOf(v)
  if type(v) == 'number' then return v end
  if type(v) == 'string' then return tonumber(v:gsub('^0x', ''), 16) end
end
local function sleepms(ms) if sleep then sleep(ms) end end

------------------------------------------------------------------ injected backend
local Pipe = {}
GM.Pipe = Pipe
function Pipe.name(pid) return 'RunnerCE-' .. pid end
-- One request per connection (server contract). Returns decoded page or nil, err.
function Pipe.request(pid, line)
  for attempt = 1, 12 do
    local p = connectToPipe(Pipe.name(pid), GM.timeout)
    if p then
      p.writeString(line .. '\n')
      local n = p.readDword()
      local body = n and n > 0 and n <= 70000 and p.readString(n) or nil
      p.destroy()
      if not body then return nil, 'pipe read failed' end
      local ok, page = pcall(json.decode, body)
      if not ok then return nil, 'bad reply: ' .. tostring(page) end
      if page.error == 'Inspector busy' or page.error == 'Inspector request expired' then
        sleepms(15 * attempt)
      else
        if page.error then return nil, page.error end
        return page
      end
    else
      -- single-instance pipe: it stays busy ~250ms after a client leaves without a request
      sleepms(60)
    end
  end
  GM.cached = nil
  return nil, 'RunnerCE pipe unavailable or busy (is RunnerEditor.dll loaded?)'
end
-- Follows hasMore paging; pages are independent samples, so de-duplicate by key.
function Pipe.all(pid, prefix, key)
  local items, seen, offset, room = {}, {}, 0, nil
  for _ = 1, GM.pageLimit do
    local page, err = Pipe.request(pid, prefix .. offset)
    if not page then return nil, err end
    room = page.room or room
    for _, it in ipairs(page.items or {}) do
      local k = it[key]
      if k ~= nil and not seen[k] then seen[k] = true; items[#items + 1] = it end
    end
    if not page.hasMore then break end
    offset = page.offset + #(page.items or {})
  end
  return items, nil, room
end

local Injected = { name = 'injected (RunnerEditor.dll)' }
function Injected.available(pid)
  -- a real request: the single-instance pipe can be briefly busy after a previous client
  return Pipe.request(pid, 'instances 1000000') ~= nil
end
local function normMember(it)
  return { name = it.name, type = it.type, value = it.value, kind = it.kind,
           address = addrOf(it.address), builtin = it.source == 'builtin' }
end
function Injected.globals(pid)
  local items, err, room = Pipe.all(pid, 'globals ', 'name')
  if not items then return nil, err end
  for i, it in ipairs(items) do items[i] = normMember(it) end
  return items, nil, room
end
function Injected.instances(pid)
  local items, err, room = Pipe.all(pid, 'instances ', 'id')
  if not items then return nil, err end
  for i, it in ipairs(items) do items[i] = { id = it.id, object = it.object, address = addrOf(it.address) } end
  return items, nil, room
end
function Injected.members(pid, inst)
  local items, err = Pipe.all(pid, 'instance ' .. inst.id .. ' ', 'name')
  if not items then return nil, err end
  for i, it in ipairs(items) do items[i] = normMember(it) end
  return items
end

------------------------------------------------------------------ external backend (DELTARUNE layout)
-- No injection. Offsets match the DELTARUNE build documented in RE/; validated by name lookup.
local L = {
  module = 'DELTARUNE.exe', global = 0x6A9DC0, names = { 0x8C9EE0, 0x5FCD08 }, varhash = 72,
  slot = 16, nameBias = 100000, instMgr = 0x6A9DC8, instMap = 136, instSlot = 24,
  instKind = 0x7C, instX = 232, instY = 236, maxSlots = 1 << 20,
}
GM.layout = L
local function rq(a) if not a or a == 0 then return nil end local v = readQword(a); if not v or v == 0 then return nil end return v end
local function ri(a) if not a or a == 0 then return nil end return readInteger(a) end

local function eachVar(obj, fn) -- YYObjectBase var map: slots {RValue* @0, nameIndex @8, hash @12}
  local hm = rq(obj + L.varhash); if not hm then return end
  local n, slots = ri(hm) or 0, rq(hm + 16)
  if not slots or n <= 0 or n > L.maxSlots then return end
  for i = 0, n - 1 do
    local s = slots + i * L.slot
    local h = ri(s + 12)
    if h and h > 0 then
      local rv = rq(s)
      if rv then fn(rv, ri(s + 8) or 0) end
    end
  end
end
local function varName(tbl, idx)
  if not tbl or idx < L.nameBias then return nil end
  local p = rq(tbl + 8 * (idx - L.nameBias)); if not p then return nil end
  local s = readString(p, 63, false)
  if s and #s > 0 and s:match('^[%w_@]+$') then return s end
end
local External = { name = 'external (DELTARUNE memory layout)' }
function External.nameTable()
  local b = getAddressSafe(L.module); if not b or b == 0 then return nil end
  local g = rq(b + L.global); if not g then return nil end
  for _, rva in ipairs(L.names) do
    local tbl = rq(b + rva)
    if tbl then
      local seen = {}
      eachVar(g, function(_, idx) local n = varName(tbl, idx); if n then seen[n] = true end end)
      if seen.gold or seen.hp or seen.maxhp then return tbl, b, g end
    end
  end
end
function External.available() return External.nameTable() ~= nil end
local function members(obj, tbl)
  local out, seen = {}, {}
  eachVar(obj, function(rv, idx)
    local name = varName(tbl, idx) or ('var#' .. idx)
    if not seen[name] then
      seen[name] = true
      local kind = (ri(rv + 12) or -1) & 0xFFFFFF
      local v
      if kind == 0 or kind == 13 then v = tostring(readDouble(rv))
      elseif kind == 7 then v = tostring(readInteger(rv, true))
      elseif kind == 10 then v = tostring(readQword(rv))
      elseif kind == 1 then local rs = rq(rv); v = rs and readString(rq(rs) or 0, 200, false) or '?'
      else v = '[' .. (KIND[kind] or 'unknown') .. ']' end
      out[#out + 1] = { name = name, type = KIND[kind] or 'unknown', value = v, kind = kind, address = rv }
    end
  end)
  table.sort(out, function(a, b) return a.name < b.name end)
  return out
end
function External.globals()
  local tbl, _, g = External.nameTable()
  if not tbl then return nil, 'external layout does not match this process' end
  return members(g, tbl)
end
function External.instances()
  local _, b = External.nameTable(); if not b then return nil, 'external layout does not match' end
  local mgr = rq(b + L.instMgr); local hm = mgr and rq(mgr + L.instMap)
  if not hm then return nil, 'instance manager unavailable' end
  local cap, slots = ri(hm), rq(hm + 16)
  if not slots or not cap or cap <= 0 or cap > L.maxSlots then return nil, 'bad instance map' end
  local out = {}
  for i = 0, cap - 1 do
    local s = slots + i * L.instSlot
    local inst = (ri(s + 16) or 0) ~= 0 and rq(s + 8)
    if inst and ri(inst + L.instKind) == 1 then
      out[#out + 1] = { id = #out + 1, address = inst,
        object = string.format('instance @%X (x=%.0f y=%.0f)', inst, readFloat(inst + L.instX) or 0, readFloat(inst + L.instY) or 0) }
    end
  end
  return out
end
function External.members(_, inst)
  local tbl = External.nameTable(); if not tbl then return nil, 'external layout does not match' end
  return members(inst.address, tbl)
end

------------------------------------------------------------------ backend selection
function GM.pid() local p = getOpenedProcessID(); if p and p ~= 0 then return p end end
function GM.backend()
  local pid = GM.pid(); if not pid then return nil, 'attach Cheat Engine to a game first' end
  if GM.cached == pid then return Injected, pid end
  if Injected.available(pid) then GM.cached = pid; return Injected, pid end
  if External.available() then return External, pid end
  return nil, 'no RunnerCE pipe and no known external layout in this process'
end
GM.Injected, GM.External = Injected, External

------------------------------------------------------------------ address list
local VT = { [0] = vtDouble, [13] = vtDouble, [7] = vtDword, [10] = vtQword }
function GM.addRecord(item, prefix, header, label)
  if not item.address then return nil, 'no address (builtins are copies; use the variable map)' end
  local al = getAddressList()
  header = header or GM.header
  if not header or not pcall(function() return header.Description end) then
    header = al.createMemoryRecord(); header.Description = 'GameMaker runner'; header.IsGroupHeader = true
    GM.header = header
  end
  local mr = al.createMemoryRecord()
  mr.Description = label or string.format('%s%s (%s)', prefix or '', item.name, item.type or '?')
  mr.Address = hex(item.address)
  if item.kind == 1 then -- RValue -> RefString* -> char*
    mr.Type = vtString; mr.String.Size = 128
    mr.OffsetCount = 2; mr.Offset[0] = 0; mr.Offset[1] = 0
  else
    mr.Type = VT[item.kind] or vtQword
  end
  mr.appendToEntry(header)
  return mr
end

------------------------------------------------------------------ console API
function gm_backend() local b, pid = GM.backend(); print(b and (b.name .. ' pid ' .. pid) or ('[!] ' .. pid)); return b end
local function list(fn, ...)
  local b, pid = GM.backend(); if not b then print('[!] ' .. pid); return end
  local items, err = b[fn](pid, ...); if not items then print('[!] ' .. err); return end
  return items
end
function gm_globals()
  local items = list('globals'); if not items then return end
  for _, it in ipairs(items) do print(string.format('%-32s %-8s %-24s %s', it.name, it.type, it.value, hex(it.address))) end
  print(#items .. ' globals'); return items
end
function gm_instances()
  local items = list('instances'); if not items then return end
  for _, it in ipairs(items) do print(string.format('%8s  %-32s %s', it.id, it.object, hex(it.address))) end
  print(#items .. ' instances'); return items
end
function gm_members(id)
  local b, pid = GM.backend(); if not b then print('[!] ' .. pid); return end
  local inst
  for _, it in ipairs(b.instances(pid) or {}) do if it.id == id then inst = it end end
  if not inst then print('[!] instance not found'); return end
  local items, err = b.members(pid, inst); if not items then print('[!] ' .. err); return end
  for _, it in ipairs(items) do print(string.format('%-32s %-8s %-24s %s', it.name, it.type, it.value, hex(it.address))) end
  return items
end
function gm_find(name)
  local b, pid = GM.backend(); if not b then return nil, pid end
  for _, it in ipairs(b.globals(pid) or {}) do if it.name == name then return it.address, it end end
end

------------------------------------------------------------------ game profiles
-- Friendly cheat entries resolved by NAME on every attach, so restarts/rooms never break them.
-- scope 'global' or an object name (first live instance). Add games from another autorun file:
--   GMRunner.profiles['Game.exe'] = { title = '...', entries = { {'Label', 'oObj', 'var'}, ... } }
GM.profiles = GM.profiles or {}
GM.profiles['Dwarf Eats Mountain.exe'] = { title = 'Dwarf Eats Mountain', entries = {
  { 'Gold (G)', 'oSys', 'gold' },
  { 'Prestige points (crown)', 'oSys', 'prestigePoints' },
  { 'Soul (flame)', 'oSys', 'soul' },
  { 'Mithril (gem)', 'oSys', 'mithril' },
  { 'Dwarves', 'oSys', 'dwarves' },
  { 'Faith', 'oSys', 'faith' },
  { 'Prestige points spendable', 'oSys', 'prestigeSpendable' },
  { 'Mountain level', 'oSys', 'mountainLevel' },
  { 'Mountain health', 'oSys', 'mountainHealth' },
  { 'Ore per minute (estimate, display only)', 'oSys', 'opm_total_est' },
  { 'Damage per minute (display only)', 'global', 'dpm_total_all' },
}}
GM.profiles['DELTARUNE.exe'] = { title = 'DELTARUNE', entries = {
  { 'Gold (D$)', 'global', 'gold' },
  { 'Chapter', 'global', 'chapter' },
  { 'Plot flag', 'global', 'plot' },
}}
local function processName()
  local ok, name = pcall(function() return process end)
  if ok and type(name) == 'string' and name ~= '' then return name end
  for exe in pairs(GM.profiles) do if getAddressSafe(exe) then return exe end end
end
-- Resolve one profile to {label, item} using the active backend; returns list, missing.
function GM.resolveProfile(profile)
  local b, pid = GM.backend(); if not b then return nil, pid end
  local scopes, found, missing = {}, {}, {}
  local insts
  for _, e in ipairs(profile.entries) do
    local label, scope, var = e[1], e[2], e[3]
    if not scopes[scope] then
      if scope == 'global' then scopes[scope] = b.globals(pid) or {}
      else
        insts = insts or b.instances(pid) or {}
        local inst
        for _, it in ipairs(insts) do if it.object == scope then inst = it; break end end
        scopes[scope] = inst and (b.members(pid, inst) or {}) or {}
      end
    end
    local hit
    for _, it in ipairs(scopes[scope]) do if it.name == var then hit = it; break end end
    if hit and hit.address then found[#found + 1] = { label = label, scope = scope, item = hit }
    else missing[#missing + 1] = scope .. '.' .. var end
  end
  return found, missing
end
-- Adds (or refreshes) a "<title> (GameMaker)" group in the address list.
function gm_profile(exe)
  exe = exe or processName()
  local profile = exe and GM.profiles[exe]
  if not profile then print('[!] no profile for ' .. tostring(exe)); return nil, 'no profile for ' .. tostring(exe) end
  local found, missing = GM.resolveProfile(profile)
  if not found then print('[!] ' .. tostring(missing)); return nil, missing end
  local al, title = getAddressList(), profile.title .. ' (GameMaker)'
  local header = al.getMemoryRecordByDescription(title)
  if header then while header.Count > 0 do header.Child[0].destroy() end
  else header = al.createMemoryRecord(); header.Description = title; header.IsGroupHeader = true end
  for _, f in ipairs(found) do
    GM.addRecord(f.item, nil, header, string.format('%s   [%s.%s]', f.label, f.scope, f.item.name))
  end
  for _, m in ipairs(missing) do print('[?] not found right now: ' .. m) end
  print(string.format('[*] %s: %d entries%s', profile.title, #found, #missing > 0 and (', ' .. #missing .. ' missing') or ''))
  return header, found, missing
end

------------------------------------------------------------------ dissector form
local function buildForm()
  local f = createForm(false)
  f.Caption = 'GameMaker Runner Dissector'; f.Width = 760; f.Height = 620; f.Position = poScreenCenter
  local top = createPanel(f); top.Align = alTop; top.Height = 32; top.BevelOuter = bvNone
  local refresh = createButton(top); refresh.Caption = 'Refresh'; refresh.Left = 4; refresh.Top = 4; refresh.Width = 80
  local filter = createEdit(top); filter.Left = 90; filter.Top = 5; filter.Width = 220; filter.TextHint = 'filter variables'
  local status = createLabel(top); status.Left = 320; status.Top = 9
  local tv = createTreeView(f); tv.Align = alClient; tv.ReadOnly = true; tv.RightClickSelect = true; tv.HideSelection = false
  GM.tv = tv
  local info, nextId = {}, 0
  local function node(parent, text, data, lazy)
    local n = parent and parent.add(text) or tv.Items.add(text)
    nextId = nextId + 1; info[nextId] = data; n.Data = nextId
    if lazy then n.HasChildren = true end
    return n
  end
  local function memberText(it)
    return string.format('%s : %s = %s%s', it.name, it.type, it.value,
      it.address and ('   [' .. hex(it.address) .. ']') or (it.builtin and '   [builtin copy]' or ''))
  end
  local function fill(parent, items, prefix)
    local pat = filter.Text ~= '' and filter.Text:lower() or nil
    for _, it in ipairs(items) do
      if not pat or it.name:lower():find(pat, 1, true) then node(parent, memberText(it), { member = it, prefix = prefix }) end
    end
  end
  local backend, pid
  local function load()
    tv.Items.clear(); info = {}
    backend, pid = GM.backend()
    if not backend then status.Caption = tostring(pid); return end
    status.Caption = backend.name .. '  pid ' .. pid
    local g = node(nil, 'Globals', { kind = 'globals' }, true)
    node(nil, 'Instances (by object)', { kind = 'objects' }, true)
    g.Expanded = true
  end
  tv.OnExpanding = function(_, n)
    local d = info[n.Data]
    if not d or not d.kind or d.loaded or not backend then return true end
    d.loaded = true; n.HasChildren = false
    if d.kind == 'globals' then
      local items, err, room = backend.globals(pid)
      if not items then node(n, '[!] ' .. err, {}) return true end
      table.sort(items, function(a, b) return a.name < b.name end)
      n.Text = string.format('Globals (%d)%s', #items, room and ('  room ' .. room) or '')
      fill(n, items, 'global.')
    elseif d.kind == 'objects' then
      local items, err = backend.instances(pid)
      if not items then node(n, '[!] ' .. err, {}) return true end
      local groups, order = {}, {}
      for _, it in ipairs(items) do
        local key = backend == Injected and it.object or 'instances'
        if not groups[key] then groups[key] = {}; order[#order + 1] = key end
        table.insert(groups[key], it)
      end
      table.sort(order)
      for _, key in ipairs(order) do
        local o = node(n, string.format('%s (%d)', key, #groups[key]), {})
        for _, it in ipairs(groups[key]) do
          node(o, string.format('%s  id %s  @%s', it.object, it.id, hex(it.address)), { kind = 'instance', inst = it }, true)
        end
      end
      n.Text = string.format('Instances (%d)', #items)
    elseif d.kind == 'instance' then
      local items, err = backend.members(pid, d.inst)
      if not items then node(n, '[!] ' .. err, {}) return true end
      fill(n, items, (d.inst.object or 'inst') .. '.')
    end
    return true
  end
  local pm = createPopupMenu(tv); tv.PopupMenu = pm
  local function selected() local s = tv.Selected; return s and info[s.Data] end
  local function item(caption, fn)
    local mi = createMenuItem(pm); mi.Caption = caption; mi.OnClick = function() local d = selected(); if d then fn(d) end end
    pm.Items.add(mi)
  end
  item('Add to address list', function(d)
    if d.member then local ok, err = GM.addRecord(d.member, d.prefix); if not ok then messageDialog(err, mtInformation, mbOK) end end
  end)
  item('Register symbol (name -> RValue address)', function(d)
    if d.member and d.member.address then registerSymbol((d.prefix or '') .. d.member.name, d.member.address, true) end
  end)
  item('Browse memory here', function(d)
    local a = (d.member and d.member.address) or (d.inst and d.inst.address)
    if a then getMemoryViewForm().HexadecimalView.Address = a; getMemoryViewForm().show() end
  end)
  item('Copy address', function(d)
    local a = (d.member and d.member.address) or (d.inst and d.inst.address)
    if a then writeToClipboard(hex(a)) end
  end)
  refresh.OnClick = load
  filter.OnKeyPress = function(_, key) if key == '\r' then load() end return key end
  f.OnClose = function() return caHide end
  return f, load
end

function GM.show()
  if not GM.form then GM.form, GM.reload = buildForm() end
  GM.form.show(); GM.reload()
end

------------------------------------------------------------------ main menu
local function installMenu()
  if GM.menu or not MainForm or not MainForm.Menu then return end
  local mfm = MainForm.Menu
  local top = createMenuItem(MainForm); top.Caption = 'GameMaker'
  mfm.Items.insert(mfm.Items.Count - 1, top)
  local m1 = createMenuItem(top); m1.Caption = 'Dissect runner...'; m1.OnClick = GM.show; top.add(m1)
  local m3 = createMenuItem(top); m3.Caption = 'Add game cheat entries (profile)'; m3.OnClick = function() gm_profile() end; top.add(m3)
  local m2 = createMenuItem(top); m2.Caption = 'Show backend in Lua console'; m2.OnClick = gm_backend; top.add(m2)
  GM.menu = top
end
if MainForm then installMenu() end

------------------------------------------------------------------ headless self-test (RUNNERCE_SELFTEST=<report path>)
local report = os.getenv and os.getenv('RUNNERCE_SELFTEST')
if report and report ~= '' and createTimer then
  local t = createTimer(nil, false); t.Interval = 500; local tries = 0
  t.OnTimer = function()
    tries = tries + 1
    if getOpenedProcessID() == 0 then openProcess(os.getenv('RUNNERCE_TARGET') or 'DELTARUNE.exe') end
    local b, pid = GM.backend()
    if os.getenv('RUNNERCE_BACKEND') == 'external' and GM.pid() then
      b, pid = External.available() and External or nil, GM.pid()
    end
    if not b and tries < 60 then return end
    t.Enabled = false
    local out = {}
    local function w(s) out[#out + 1] = s end
    if not b then w('FAIL ' .. tostring(pid)) else
      w('backend ' .. b.name .. ' pid ' .. pid)
      local g, err = b.globals(pid); w('globals ' .. (g and #g or ('ERR ' .. err)))
      local checked = 0
      for _, it in ipairs(g or {}) do
        if (it.kind == 0 or it.kind == 7 or it.kind == 10) and it.address and checked < 25 then
          checked = checked + 1
          local live = it.kind == 0 and readDouble(it.address) or it.kind == 7 and readInteger(it.address, true) or readQword(it.address)
          local kind = (readInteger(it.address + 12) or -1) & 0xFFFFFF
          w(string.format('check %s pipe=%s mem=%s kind=%d/%d %s', it.name, it.value, tostring(live), it.kind, kind,
            (kind == it.kind) and 'OK' or 'MISMATCH'))
        end
      end
      local inst = b.instances(pid); w('instances ' .. (inst and #inst or 'ERR'))
      if inst and inst[1] then
        local m = b.members(pid, inst[1]); w('members of ' .. inst[1].object .. ' ' .. (m and #m or 'ERR'))
      end
      local rec = g and g[1] and GM.addRecord((function() for _, it in ipairs(g) do if it.kind == 0 and it.address then return it end end end)() or g[1], 'global.')
      w('record ' .. (rec and (rec.Description .. ' = ' .. tostring(rec.Value)) or 'none'))
    end
    if b and os.getenv('RUNNERCE_PROFILE') == '1' then
      local ok, err = pcall(function()
        local header, found, missing = gm_profile(os.getenv('RUNNERCE_TARGET'))
        if not header then error('gm_profile: ' .. tostring(found)) end
        w('profile ' .. header.Description .. ' entries ' .. header.Count .. ' missing ' .. #missing)
        for i = 0, header.Count - 1 do
          local mr = header.Child[i]; w('  ' .. mr.Description .. ' = ' .. tostring(mr.Value) .. ' @' .. mr.Address)
        end
        local gold = header.Child[0]
        local before = tonumber(gold.Value)
        gold.Value = tostring(before + 1000)
        local after = readDouble(getAddress(gold.Address))
        w(string.format('write gold %.3f -> %.3f (delta %.3f)', before, after, after - before))
        sleep(1500)
        local again = GM.resolveProfile({ entries = { { 'g', 'oSys', 'gold' } } })
        w('game sees gold after 1.5s: ' .. tostring(again[1] and again[1].item.value))
        gold.Value = tostring(readDouble(getAddress(gold.Address)) - 1000)
        w(string.format('restored gold %.3f', readDouble(getAddress(gold.Address))))
      end)
      if not ok then w('profile ERR ' .. tostring(err)) end
    end
    if b and os.getenv('RUNNERCE_UI') == '1' then
      local ok, err = pcall(function()
        GM.show()
        local root = GM.tv.Items[0]; root.Expanded = true
        w('ui ' .. root.Text .. ' children ' .. root.Count .. ' first: ' .. root.Items[0].Text)
        local objs = root.getNextSibling(); root.Expanded = false; objs.Expanded = true
        w('ui ' .. objs.Text .. ' groups ' .. objs.Count .. ' first: ' .. (objs.Count > 0 and objs.Items[0].Text or '-'))
        local group = objs.Items[0]; group.Expanded = true
        local inst = group.Items[0]; inst.Expanded = true
        w('ui ' .. objs.Text .. ' / ' .. group.Text .. ' / ' .. inst.Text .. ' members ' .. inst.Count)
        GM.tv.Selected = inst.Items[inst.Count - 1]
      end)
      if not ok then w('ui ERR ' .. tostring(err)) end
    end
    local fh = io.open(report, 'w'); fh:write(table.concat(out, '\n'), '\n'); fh:close()
    if os.getenv('RUNNERCE_EXIT') == '1' then
      local q = createTimer(nil, false); q.Interval = tonumber(os.getenv('RUNNERCE_LINGER') or '0') + 1
      q.OnTimer = function() q.Enabled = false; closeCE() end; q.Enabled = true
    end
  end
  t.Enabled = true
end
