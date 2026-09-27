--[[============================================================================
 DELTARUNE stat resolver for Cheat Engine          (GameMaker VC_Runner engine)
 Synced with deltarune_stats_pince.py (2026-09-27).

 Paste into CE (Memory View > Tools > Lua Engine, or Table > Show Cheat Table Lua
 Script), execute, then call  dr_find()  (it also runs once on load).
 Adds live, auto-refreshed "[DR]" entries to your address list for gold, TP, EXP,
 LV, HP/MaxHP, AT/DF/MAG/GUTS, equipped weapon/armor ids, and inventory.
 Tick a box to freeze; double-click Value to edit.  dr_stop() removes them.

 Commands (Lua Engine):
   dr_find()                 resolve everything and (re)create the [DR] rows
   dr_stop()                 remove rows, stop timers, disable the damage guard
   dr_nodamage(true/false)   hold global.inv (invulnerability timer) -- or tick the
                             "[DR] No damage (global.inv guard)" row
   dr_instances([kind],[limit])   list live GML instances + x/y (read-only;
                             kind defaults to 1 = real CInstance, false = all kinds)

 PRIMARY MODE = "by name" (no config needed):
   Reads the game's own variable-name table, so it finds each global by its real
   name (hp, maxhp, gold, ...). Also resolves zero-valued arrays (inventory).

 FALLBACK MODE = "by value" (only if the name table can't be read):
   Matches CFG.hp_party against arrays and gold/TP by value. Ambiguous scalar
   matches (more than one candidate) are refused, not guessed.

 Character ids: 1=Kris 2=Susie 3=Ralsei 4=Noelle (id 4 is initialized from Ch2 on;
 in Ch1 her rows show as unresolved).
 Inventory capacities: item/keyitem 13; weapon/armor 48 and pocketitem 72 from Ch2
 on (Ch1 is shorter). Slots outside the live array, or non-REAL, are skipped.

 LAYOUT (reverse-engineered, see ..\07_globals_and_variable_storage.md, ..\08_instances.md):
   g_pGlobalObject = [DELTARUNE.exe+006A9DC0]
   varHashMap      = [global+72]   ( +0 numSlots, +8 mask, +16 slots )
   slot (16B)      = +0 RValue* value | +8 nameIndex | +12 (var_id+1, <=0 empty)
   RValue (16B)    = +0 double/ptr | +8 flags | +12 kind(&0xFFFFFF)  (0=REAL,2=ARRAY)
   GMArray         = +0x90 elements (RValue*, stride16) | +0xA4 length
   name table ptr  = [DELTARUNE.exe+008C9EE0]; name = [ [tbl]+8*(nameIndex-100000) ]
   alt name table  = [DELTARUNE.exe+005FCD08]
   g_pInstanceManager = [DELTARUNE.exe+006A9DC8]
   instance hashmap   = [ [mgr+136] ]  (+136 is a POINTER to the header)
                        +0 capacity u32 | +16 slots ptr
                        slot 24B = +0 value | +8 key (= instance) | +16 hash (0 = empty)
   YYObjectBase.kind  = [inst+0x7C] (1 = real CInstance) ; CInstance x/y = float @+232/+236

 LIMITS
   * Re-run dr_find() after changing chapters (each chapter is a new process).
   * "No damage" guards the normal global.inv check only. Some scripted hazards
     (e.g. Ch2 teacups) set inv=-1 right before damaging, and direct HP writes
     bypass it. It does not heal or revive downed party members.
   * dr_instances() can't tell WHICH object an instance is (typeIndex is
     debug-build-only in this retail runner).
=============================================================================--]]

----------------------------------------------------------------- FALLBACK CONFIG
-- Only used if the by-name table can't be read. Put your CURRENT values here.
local CFG = {
  hp_party = { 90, 110, 70 },   -- current HP for char id 1,2,3 (Kris,Susie,Ralsei)
  gold     = 0,
  tension  = 0,
}

local PARTY      = { [1]="Kris", [2]="Susie", [3]="Ralsei", [4]="Noelle" }   -- char id -> name
local PARTY_IDS  = { 1, 2, 3, 4 }
local REFRESH_MS       = 1000
local GUARD_REFRESH_MS = 50
local GUARD_FRAMES     = 120.0   -- value held in global.inv while the guard is on

-- Which globals to expose, and how. kind: "scalar" | "array".
--   chars=true   -> expose party ids 1..4
--   count=N      -> expose array indices 0..N-1 (only those that exist and are REAL)
local STATS = {
  { var="gold",       kind="scalar", label="Gold" },
  { var="tension",    kind="scalar", label="TP (tension)" },
  { var="maxtension", kind="scalar", label="Max TP" },
  { var="xp",         kind="scalar", label="EXP" },
  { var="lv",         kind="scalar", label="LV" },
  { var="hp",         kind="array",  chars=true, label="HP" },
  { var="maxhp",      kind="array",  chars=true, label="MaxHP" },
  { var="at",         kind="array",  chars=true, label="AT" },
  { var="df",         kind="array",  chars=true, label="DF" },
  { var="mag",        kind="array",  chars=true, label="MAG" },
  { var="guts",       kind="array",  chars=true, label="GUTS" },
  { var="charweapon", kind="array",  chars=true, label="Weapon id" },
  { var="chararmor1", kind="array",  chars=true, label="Armor1 id" },
  { var="chararmor2", kind="array",  chars=true, label="Armor2 id" },
  { var="item",       kind="array",  count=13,   label="Item" },
  { var="keyitem",    kind="array",  count=13,   label="KeyItem" },
  -- Ch2+ scr_gamestart initializes 48 equipment / 72 pocket slots; Ch1 is shorter.
  { var="weapon",     kind="array",  count=48,   label="WeaponStock" },
  { var="armor",      kind="array",  count=48,   label="ArmorStock" },
  { var="pocketitem", kind="array",  count=72,   label="PocketItem" },
}

----------------------------------------------------------------------- LAYOUT
local MODULE       = "DELTARUNE.exe"
local RVA_GLOBAL   = 0x6A9DC0
local RVA_NAMES    = 0x8C9EE0   -- pointer to user-var name table
local RVA_NAMES_A  = 0x5FCD08   -- alternate name table pointer
local OFF_VARHASH  = 72
local SLOT_STRIDE  = 16
local ARR_ELEMS    = 0x90
local ARR_LEN      = 0xA4
local NAME_BIAS    = 100000
local MAX_SLOTS    = (16 << 20) // SLOT_STRIDE

local RVA_INSTANCE_MGR     = 0x6A9DC8
local OFF_INSTANCE_HASHMAP = 136
local INSTANCE_SLOT_STRIDE = 24
local OFF_INSTANCE_KIND    = 0x7C
local OFF_INSTANCE_X       = 232
local OFF_INSTANCE_Y       = 236
local KIND_CINSTANCE       = 1
local MAX_INSTANCE_SLOTS   = (16 << 20) // INSTANCE_SLOT_STRIDE

----------------------------------------------------------------------- HELPERS
-- rq treats a NULL pointer as nil (in Lua 0 is truthy, so `if not p` wouldn't catch it).
local function rq(a) if not a or a==0 then return nil end local v=readQword(a); if v==0 then return nil end return v end
local function ri(a) if not a or a==0 then return nil end return readInteger(a) end
local function rd(a) if not a or a==0 then return nil end return readDouble(a) end

local function moduleBase()
  local b = getAddressSafe(MODULE)
  if not b or b==0 then print("[!] DELTARUNE.exe not found - attach CE first"); return nil end
  return b
end
local function globalObj() local b=moduleBase(); if not b then return nil end return rq(b+RVA_GLOBAL) end

-- enumerate occupied slots of the global variable hashmap: fn(rvaluePtr, varId, nameIndex)
local function eachGlobalVar(fn)
  local g = globalObj(); if not g then return end
  local hm = rq(g+OFF_VARHASH); if not hm then return end
  local n  = ri(hm) or 0
  local slots = rq(hm+16)
  if not slots or n<=0 or n>MAX_SLOTS then return end
  for i=0,n-1 do
    local slot = slots + i*SLOT_STRIDE
    local key  = ri(slot+12)
    if key and key>0 then
      local rv = rq(slot+0)
      if rv and rv~=0 then fn(rv, key-1, ri(slot+8) or 0) end
    end
  end
end

local function kindOf(rv) local k=rv and ri(rv+12); return k and (k & 0xFFFFFF) or -1 end

-- Write a double only into a REAL RValue (kind 0), leaving flags/kind untouched.
local function writeReal(addr, v)
  if not addr or kindOf(addr) ~= 0 then return false end
  return writeDouble(addr, v) and true or false
end

-------------------------------------------------------------- NAME RESOLUTION
local NAME = { ok=false, base=nil }
local function plausible(s) return s and #s>0 and #s<64 and s:match("^[%w_@]+$")~=nil end

local function readVarName(tblPtr, nameIndex)
  if not tblPtr or tblPtr==0 or nameIndex < NAME_BIAS then return nil end
  local p = rq(tblPtr + 8*(nameIndex-NAME_BIAS))
  if not p or p==0 then return nil end
  local s = readString(p, 63, false)
  if plausible(s) then return s end
  return nil
end

-- Try primary then alternate name table; validate by requiring a known name.
local function detectNameTable()
  local b = moduleBase(); if not b then return false end
  for _,rva in ipairs({ RVA_NAMES, RVA_NAMES_A }) do
    local tbl = rq(b+rva)
    if tbl and tbl~=0 then
      local seen = {}
      eachGlobalVar(function(_,_,nameIndex)
        local nm = readVarName(tbl, nameIndex); if nm then seen[nm]=true end
      end)
      if seen.hp or seen.gold or seen.maxhp then
        NAME.ok, NAME.base = true, tbl
        return true
      end
    end
  end
  NAME.ok, NAME.base = false, nil
  return false
end

-- build { varname -> rvaluePtr } for this instant
local function mapByName()
  local m = {}
  if not NAME.ok then return m end
  eachGlobalVar(function(rv,_,nameIndex)
    local nm = readVarName(NAME.base, nameIndex)
    if nm then m[nm] = rv end
  end)
  return m
end

----------------------------------------------------------- ADDRESS COMPUTATION
-- array rvalue -> element address for index i (bounds- and REAL-checked)
local function arrElemAddr(rv, i)
  if not rv or kindOf(rv) ~= 2 then return nil end
  local arr = rq(rv); if not arr then return nil end
  local buf = rq(arr+ARR_ELEMS); local len = ri(arr+ARR_LEN)
  if not buf or not len or i<0 or i>=len then return nil end
  local elem = buf + 16*i
  if kindOf(elem) ~= 0 then return nil end
  return elem
end

-- Each spec: { desc=, get=function(snap)->addr }. snap = mapByName(), taken once per tick.
local function buildSpecs_byName()
  local specs = {}
  local m = mapByName()
  for _,s in ipairs(STATS) do
    local var = s.var
    if m[var] then
      if s.kind=="scalar" then
        specs[#specs+1] = { desc=string.format("%s  (global.%s)", s.label, var),
                            get=function(snap) local rv=snap[var]; return (kindOf(rv)==0) and rv or nil end }
      else
        local idxs = {}
        if s.chars then idxs = PARTY_IDS
        elseif s.count then for i=0,s.count-1 do idxs[#idxs+1]=i end end
        for _,ix in ipairs(idxs) do
          -- Inventory capacities differ by chapter: omit absent / non-REAL slots.
          if not (s.count and arrElemAddr(m[var], ix)==nil) then
            local who = s.chars and (PARTY[ix] or ("id"..ix)) or ("["..ix.."]")
            specs[#specs+1] = { desc=string.format("%s %s  (global.%s[%d])", s.label, who, var, ix),
                                get=function(snap) return arrElemAddr(snap[var], ix) end }
          end
        end
      end
    end
  end
  return specs
end

----------------------------------------------------- VALUE FALLBACK (health etc)
local function approx(a,b) return a and b and math.abs(a-b)<0.5 end
local function buildSpecs_byValue()
  local specs = {}
  -- arrays (len>=4) whose [1..3] match CFG.hp_party -> hp and/or maxhp.
  -- Captures the RValue root; re-run dr_find() after chapter changes.
  local healthRvs = {}
  eachGlobalVar(function(rv)
    if kindOf(rv)==2 then
      local arr=rq(rv); local buf=arr and rq(arr+ARR_ELEMS); local len=arr and ri(arr+ARR_LEN)
      if buf and len and len>=4 then
        local match=true
        for id=1,3 do if not approx(rd(buf+16*id), CFG.hp_party[id]) then match=false end end
        if match then healthRvs[#healthRvs+1]=rv end
      end
    end
  end)
  for n,rv in ipairs(healthRvs) do
    for id=1,3 do
      specs[#specs+1] = { desc=string.format("Health Value %d - %s", n, PARTY[id]),
                          get=function() return arrElemAddr(rv, id) end }
    end
  end
  local function scalarByValue(val,label)
    local hits = {}
    eachGlobalVar(function(rv) if kindOf(rv)==0 and approx(rd(rv),val) then hits[#hits+1]=rv end end)
    if #hits ~= 1 then
      print(string.format("[!] fallback: %s: %d candidates; not exposed. Set CFG to a distinctive current value.", label, #hits))
      return
    end
    local rv = hits[1]
    specs[#specs+1] = { desc=label, get=function() return kindOf(rv)==0 and rv or nil end }
  end
  scalarByValue(CFG.gold, "Gold (value-matched)")
  scalarByValue(CFG.tension, "TP (value-matched)")
  if #healthRvs==0 then print("[!] fallback: no HP-matching array found - update CFG.hp_party to current values") end
  return specs
end

----------------------------------------------------------------- DAMAGE GUARD
-- Holds global.inv (the invulnerability timer scr_damage checks) at GUARD_FRAMES.
DR = DR or {}

local function guardTick()
  if globalObj() ~= DR.guardGlobal then
    print("[!] damage guard disarmed: global object changed; run dr_find() in the new chapter")
    return false
  end
  if not writeReal(mapByName()["inv"], GUARD_FRAMES) then
    print("[!] damage guard disarmed: global.inv unresolved, non-REAL, or write failed")
    return false
  end
  return true
end

-- Keep the address-list checkbox in sync without re-triggering its script.
local function syncGuardRecord(active)
  local mr = DR.guardRecord
  if mr and mr.Active ~= active then
    DR.guardSyncing = true; mr.Active = active; DR.guardSyncing = false
  end
end

local function guardOff(resetTimer)
  if DR.guardTimer then DR.guardTimer.destroy(); DR.guardTimer=nil end
  local wasOn = DR.guard
  DR.guard = false
  if resetTimer and wasOn and globalObj() == DR.guardGlobal then
    writeReal(mapByName()["inv"], 0)   -- clear the timer rather than leave 120 frames
  end
  DR.guardGlobal = nil
  syncGuardRecord(false)
end

function dr_nodamage(enable)
  if enable == nil then enable = true end
  if not enable then
    local was = DR.guard
    guardOff(true)
    if was then print("[*] damage guard disabled") end
    return true
  end
  if DR.guard then return true end
  if not NAME.ok and not detectNameTable() then
    print("[!] damage guard needs by-name resolution (name table unreadable)")
    return false
  end
  DR.guardGlobal = globalObj()
  if not DR.guardGlobal then print("[!] damage guard: no global object - load a save first"); return false end
  if not guardTick() then DR.guardGlobal=nil; return false end
  DR.guard = true
  DR.guardTimer = createTimer(nil)
  DR.guardTimer.Interval = GUARD_REFRESH_MS
  DR.guardTimer.OnTimer = function()
    if DR.guard and not guardTick() then guardOff(false) end
  end
  syncGuardRecord(true)
  print("[*] damage guard enabled: global.inv held at "..GUARD_FRAMES.." (scripted bypasses remain)")
  return true
end

-- Adds a checkbox row to the address list that toggles the guard.
local function addGuardRecord(al)
  local mr = al.createMemoryRecord()
  mr.Description = "[DR] No damage (global.inv guard)"
  mr.Type = vtAutoAssembler
  mr.Script = "{$lua}\n[ENABLE]\nif not DR.guardSyncing and not dr_nodamage(true) then error('damage guard could not start - see Lua output') end\n[DISABLE]\nif not DR.guardSyncing then dr_nodamage(false) end\n"
  DR.guardRecord = mr
  return mr
end

------------------------------------------------------------ LIVE INSTANCES
-- Read-only: every entry in g_pInstanceManager's pointer-keyed hashmap.
function dr_instances(kind, limit)
  if kind == nil then kind = KIND_CINSTANCE end
  limit = limit or 200
  local b = moduleBase(); if not b then return end
  local mgr = rq(b+RVA_INSTANCE_MGR); if not mgr then print("[!] g_pInstanceManager null"); return end
  local hm = rq(mgr+OFF_INSTANCE_HASHMAP); if not hm then print("[!] instance hashmap null"); return end
  local cap = ri(hm); local slots = rq(hm+16)
  if not slots or not cap or cap<=0 or cap>MAX_INSTANCE_SLOTS then
    print("[!] bad instance hashmap (cap="..tostring(cap)..")"); return
  end
  local shown, total = 0, 0
  for i=0,cap-1 do
    local slot = slots + i*INSTANCE_SLOT_STRIDE
    local h = ri(slot+16)
    local inst = rq(slot+8)
    if h and h~=0 and inst then
      local k = ri(inst+OFF_INSTANCE_KIND) or -1
      if not kind or k==kind then
        total = total + 1
        if shown < limit then
          shown = shown + 1
          if k==KIND_CINSTANCE then
            print(string.format("   %X  kind=%d  x=%.1f y=%.1f", inst, k,
              readFloat(inst+OFF_INSTANCE_X) or 0, readFloat(inst+OFF_INSTANCE_Y) or 0))
          else
            print(string.format("   %X  kind=%d", inst, k))
          end
        end
      end
    end
  end
  print(string.format("[*] %d instance(s)%s%s", total, kind and (" (kind="..kind..")") or "",
                      total>shown and (", showing "..shown) or ""))
  return total
end

----------------------------------------------------------------------- DRIVER
function dr_stop()
  guardOff(true)
  if DR.timer then DR.timer.destroy(); DR.timer=nil end
  if DR.guardRecord then DR.guardRecord.Destroy(); DR.guardRecord=nil end
  if DR.records then for _,mr in ipairs(DR.records) do if mr and mr.Destroy then mr.Destroy() end end end
  DR.records=nil; DR.specs=nil
  print("[*] resolver stopped, [DR] entries removed.")
end

function dr_find()
  dr_stop()
  if not globalObj() then print("[!] global object null - load a save / be in-game, then retry") return end
  local mode, specs
  if detectNameTable() then
    mode="by-name"; specs=buildSpecs_byName()
  else
    mode="by-value (fallback)"; specs=buildSpecs_byValue()
  end
  print(string.format("[*] mode: %s   (%d entries)", mode, #specs))
  if #specs==0 then print("[!] nothing resolved - see messages above"); return end
  local al=getAddressList(); DR.records={}; DR.specs=specs
  if NAME.ok then addGuardRecord(al) end
  local snap = mapByName()
  for _,sp in ipairs(specs) do
    local addr=sp.get(snap)
    local mr=al.createMemoryRecord()
    mr.Description="[DR] "..sp.desc; mr.Type=vtDouble
    if addr then mr.Address=string.format("%X",addr) end
    DR.records[#DR.records+1]=mr
    print(string.format("   %-40s %s = %s", sp.desc, addr and string.format("@%X",addr) or "(unresolved)",
                        addr and tostring(rd(addr)) or "?"))
  end
  DR.timer=createTimer(nil); DR.timer.Interval=REFRESH_MS
  DR.timer.OnTimer = function()
    if not DR.specs then return end
    local s = mapByName()
    for i,sp in ipairs(DR.specs) do
      local a=sp.get(s)
      if a and DR.records[i] then DR.records[i].Address=string.format("%X",a) end
    end
  end
  print("[*] Done. [DR] rows added. Tick to freeze, double-click Value to edit.")
  print("    dr_nodamage(true) or tick '[DR] No damage' | dr_instances() | dr_stop()")
end

print("== DELTARUNE stat resolver loaded ==  run:  dr_find()")
print("   (by-name mode needs no config; CFG is only for the value fallback)")

dr_find()
