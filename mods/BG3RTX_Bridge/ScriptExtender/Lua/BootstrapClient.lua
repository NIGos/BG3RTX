-- BG3RTX bridge, client context (docs/SE_BRIDGE.md).
--
-- Exports the light entities near the controlled character to
--   %LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx\lights.txt
-- a few times a second, for the BG3RTX Vulkan layer (layer/sebridge.cpp). Format v1, text:
--   BG3RTX_SE 1
--   seq <n> ms <clock> level <name> host <x> <y> <z> scan_us <us> avg_us <us> max_us <us> all <n> classified <n> out <n>
--   L <id> <owner> <type> <x> <y> <z> <radius> <r> <g> <b> <intensity> <kelvin> <flags> <channel>
--     <flicker_speed> <flicker_amount> <move_speed> <move_amount> <name>
--   END
-- owner: 1 placed level light, 2 item, 3 scenery visual, 4 character, 0 unknown.
-- type: 0 point, 1 spot, 2 box ("Direction"). flags: see FLAG_* below.
-- The file is rewritten whole; the layer only accepts it when the END line is there.
--
-- Cost: lights are classified once (template file, owner) and cached by entity handle,
-- at most CLASSIFY_BUDGET per tick; placed lights are then found through a 16 m grid,
-- and only the lights within RANGE are read again: visual and character lights each export,
-- placed lights (which do not move) a quarter of them per export.

-- Two games at once (scripts/auto-run.ps1 shared runs): each gets "bg3rtxse=<token>" on
-- its command line and uses the folder bg3rtx/<token>/, as its layer does
-- (layer/sebridge.cpp se_token). Without it: bg3rtx/ as before.
local function bridge_dir()
  local ok, params = pcall(function() return Ext.Utils.GetCommandLineParams() end)
  if ok and type(params) == "table" then
    local function find(s) return type(s) == "string" and s:match("bg3rtxse=([%w_%-]+)") end
    for k, v in pairs(params) do
      local t = find(k) or find(v) or (k == "bg3rtxse" and type(v) == "string" and v:match("^([%w_%-]+)$"))
      if not t and type(v) == "table" then for _, s in pairs(v) do t = t or find(s) end end
      if t then return "bg3rtx/" .. t .. "/" end
    end
  end
  return "bg3rtx/"
end
local DIR = bridge_dir()
local OUT = DIR .. "lights.txt"
local PERIOD_US = 250000        -- 4 Hz
local RANGE = 60.0              -- m around the controlled character
local CLASSIFY_BUDGET = 100     -- new light entities classified per tick
local RESYNC_US = 5000000       -- full membership check of the light list
local CELL = 16.0

local FLAG_ENABLED, FLAG_USE_TEMPERATURE, FLAG_FILL, FLAG_FLICKER = 1, 2, 4, 8
local FLAG_MOVING, FLAG_SHADOW, FLAG_CAST_SHADOW, FLAG_VOLUMETRIC = 16, 32, 64, 128
local FLAG_PLAYER_LIGHT = 256   -- Light_Player_Darkvision_* (the light that follows the active character)
local OWNER_UNKNOWN, OWNER_PLACED, OWNER_ITEM, OWNER_SCENERY, OWNER_CHARACTER = 0, 1, 2, 3, 4
local TYPES = {Point = 0, Spot = 1, Direction = 2}

local clock = Ext.Timer.MicrosecTime
local cache = {}       -- entity -> record
local grid = {}        -- cell key -> list of placed records
local dynamic = {}     -- list of non-placed records
local pending = nil    -- entities still to classify
local pending_i = 1
local classified, n_all = 0, 0
local last, last_sync, seq = 0, 0, 0
local cost_sum, cost_n, cost_max = 0, 0, 0
local level = ""
local cls_us, cls_max = 0, 0   -- classification time, total and worst tick
local charlights = {}  -- entity -> true (CharacterLight rim/fill lights)

local function try(f, ...)
  local ok, v = pcall(f, ...)
  if ok then return v end
  return nil
end

local function host()
  for _, e in pairs(Ext.Entity.GetAllEntitiesWithComponent("ClientControl")) do
    local c = e.ClientCharacter
    if c and c.ReservedUserID == 1 then return e end
  end
end

-- Integer cell keys (world coordinates stay within +-16 km, so 2048 cells per axis).
local function ckey(x, y, z) return ((x + 1024) * 2048 + (y + 1024)) * 2048 + (z + 1024) end

local function key(x, y, z)
  return ckey(math.floor(x / CELL), math.floor(y / CELL), math.floor(z / CELL))
end

local function collect_charlights()
  local t = {}
  for _, c in ipairs(Ext.Entity.GetAllEntitiesWithComponent("CharacterLight")) do
    for _, map in pairs(c.CharacterLight.Lights) do
      for _, list in pairs(map) do
        for _, it in pairs(list) do t[it.Entity] = true end
      end
    end
  end
  charlights = t
end

-- The owner of a light whose template is a root template (a visual's light): the item
-- or character it sits on, else scenery. One query per light, once.
local function owner_near(pos)
  local around = try(Ext.Entity.GetEntitiesAroundPosition, pos, 1.0) or {}
  local scen = false
  for _, e in ipairs(around) do
    local names = try(function() return table.concat(e:GetAllComponentNames(false), ",") end) or ""
    if names:find("ecl::Item", 1, true) then return OWNER_ITEM end
    if names:find("ecl::Character", 1, true) or names:find("ClientCharacter", 1, true) then return OWNER_CHARACTER end
    if names:find("Scenery", 1, true) then scen = true end
  end
  return scen and OWNER_SCENERY or OWNER_UNKNOWN
end

local function classify(e)
  local l = e.Light
  local t = l.Template
  local r = {e = e, id = Ext.Utils.HandleToInteger(e)}
  local file = t and t.FileName or ""
  local name = t and t.Name or ""
  r.name = (name ~= "" and name or "-"):gsub("%s", "_")
  r.type = TYPES[tostring(l.LightType)] or 0
  r.static = 0
  if t then
    if t.Shadow then r.static = r.static | FLAG_SHADOW end
    if t.CastShadow then r.static = r.static | FLAG_CAST_SHADOW end
    if t.VolumetricShadow then r.static = r.static | FLAG_VOLUMETRIC end
    r.flick_speed, r.flick_amount = t.Speed or 0, t.Amount or 0
    r.move_speed, r.move_amount = t.MovementSpeed or 0, t.MovementAmount or 0
  else
    r.flick_speed, r.flick_amount, r.move_speed, r.move_amount = 0, 0, 0, 0
  end
  local p = l.WorldTransform.Translate
  r.x, r.y, r.z = p[1], p[2], p[3]
  -- fields that do not change while the entity lives, preformatted
  local c = l.Color
  r.s1 = string.format("%.3f %.4f %.4f %.4f", l.Radius, c[1], c[2], c[3])
  r.s2 = string.format("%.0f", l.Kelvin)
  if charlights[e] then
    r.owner = OWNER_CHARACTER
  elseif name:find("^Light_Player_Darkvision") then
    -- only the darkvision pair follows the controlled character; Light_Player_Fill_* are
    -- fill lights at fixed world positions and are classified like any other light below
    r.owner = OWNER_CHARACTER
    r.static = r.static | FLAG_PLAYER_LIGHT
  elseif file:find("/Levels/", 1, true) and (file:find("/Lights/", 1, true) or file:find("/CombinedLights/", 1, true)) then
    r.owner = OWNER_PLACED
  elseif file:find("RootTemplates", 1, true) then
    r.owner = owner_near(p)
  else
    r.owner = OWNER_UNKNOWN
  end
  r.s3 = string.format("%d %.3f %.3f %.3f %.3f %s", l.LightChannelFlag, r.flick_speed, r.flick_amount, r.move_speed,
    r.move_amount, r.name)
  cache[e] = r
  if r.owner == OWNER_PLACED then
    local k = key(r.x, r.y, r.z)
    local c = grid[k]
    if not c then c = {}; grid[k] = c end
    c[#c + 1] = r
  else
    dynamic[#dynamic + 1] = r
  end
  return true
end

local function reset()
  cache, grid, dynamic, classified = {}, {}, {}, 0
  pending, pending_i = nil, 1
end

-- Flag bits by the LightFlags string (a handful of distinct values per level).
local flagbits = {}
local function bits(fs)
  local f = flagbits[fs]
  if f then return f end
  f = 0
  if fs:find("Enabled", 1, true) then f = f | FLAG_ENABLED end
  if fs:find("UseTemperature", 1, true) then f = f | FLAG_USE_TEMPERATURE end
  if fs:find("FillLight", 1, true) then f = f | FLAG_FILL end
  if fs:find("IsFlickering", 1, true) then f = f | FLAG_FLICKER end
  if fs:find("IsMoving", 1, true) then f = f | FLAG_MOVING end
  flagbits[fs] = f
  return f
end

-- Reads the live state of one cached light (flags and intensity; the position too
-- unless it is a placed light) and returns its output line.
local function line(r)
  local l = r.e.Light
  if l == nil then return nil end
  if r.owner ~= OWNER_PLACED then
    local p = l.WorldTransform.Translate
    r.x, r.y, r.z = p[1], p[2], p[3]
  end
  return string.format("L %d %d %d %.4f %.4f %.4f %s %.1f %s %d %s", r.id, r.owner, r.type, r.x, r.y, r.z, r.s1,
    l.Intensity, r.s2, r.static | bits(tostring(l.Flags)), r.s3)
end

local function near(r, hx, hy, hz, range)
  local dx, dy, dz = r.x - hx, r.y - hy, r.z - hz
  return dx * dx + dy * dy + dz * dz <= range * range
end

local function tick()
  local now = clock()
  -- classification work, spread over ticks
  if pending then
    local c0 = clock()
    local stop = math.min(#pending, pending_i + CLASSIFY_BUDGET - 1)
    for i = pending_i, stop do
      local e = pending[i]
      if cache[e] == nil then
        if try(classify, e) then classified = classified + 1 else cache[e] = false end
      end
    end
    pending_i = stop + 1
    if pending_i > #pending then pending = nil end
    local dt = clock() - c0
    cls_us = cls_us + dt
    if dt > cls_max then cls_max = dt end
  end
  if now - last < PERIOD_US then return end
  last = now
  local t0 = clock()

  local h = host()
  if h == nil then return end
  local lv = try(function() return h.Level.LevelName end) or ""
  if lv ~= level then
    level = lv
    reset()
  end
  local all = Ext.Entity.GetAllEntitiesWithComponent("Light")
  if #all ~= n_all or now - last_sync > RESYNC_US then
    -- new or removed light entities: queue the unknown ones, drop the dead ones
    n_all, last_sync = #all, now
    collect_charlights()
    local q = {}
    local live = {}
    for _, e in ipairs(all) do
      live[e] = true
      if cache[e] == nil then q[#q + 1] = e end
    end
    if #q > 0 then pending, pending_i = q, 1 end
    local d = {}
    for _, r in ipairs(dynamic) do
      if live[r.e] then
        d[#d + 1] = r
        -- a visual light classified before its item or scenery streamed in: ask again (5 tries)
        if r.owner == OWNER_UNKNOWN and (r.tries or 0) < 5 then
          r.tries = (r.tries or 0) + 1
          r.owner = owner_near({r.x, r.y, r.z})
        end
      else
        cache[r.e] = nil
      end
    end
    dynamic = d
  end

  local hp = h.Transform.Transform.Translate
  local hx, hy, hz = hp[1], hp[2], hp[3]
  local out = {}
  local cx, cy, cz = math.floor(hx / CELL), math.floor(hy / CELL), math.floor(hz / CELL)
  local n = math.ceil(RANGE / CELL)
  for x = cx - n, cx + n do
    for y = cy - 2, cy + 2 do   -- +-32 m vertically
      for z = cz - n, cz + n do
        local c = grid[ckey(x, y, z)]
        if c then
          for _, r in ipairs(c) do
            if near(r, hx, hy, hz, RANGE) then
              -- placed lights do not move: their state is re-read at 1 Hz (a quarter per export)
              if r.line == nil or (seq + r.id) % 4 == 0 then r.line = try(line, r) end
              out[#out + 1] = r.line
            end
          end
        end
      end
    end
  end
  for _, r in ipairs(dynamic) do
    -- items can be carried and characters walk: test against the last position, padded
    if r.owner == OWNER_CHARACTER or near(r, hx, hy, hz, RANGE + 10.0) then
      local s = try(line, r)
      if s and near(r, hx, hy, hz, RANGE) then out[#out + 1] = s end
    end
  end

  seq = seq + 1
  local cost = clock() - t0
  cost_sum, cost_n = cost_sum + cost, cost_n + 1
  if cost > cost_max then cost_max = cost end
  local hdr = string.format("BG3RTX_SE 1\nseq %d ms %.0f level %s host %.3f %.3f %.3f scan_us %.0f avg_us %.0f max_us %.0f all %d classified %d out %d classify_us %.0f classify_max_us %.0f",
    seq, now / 1000, level ~= "" and level or "-", hx, hy, hz, cost, cost_sum / cost_n, cost_max, n_all, classified, #out, cls_us, cls_max)
  Ext.IO.SaveFile(OUT, hdr .. "\n" .. table.concat(out, "\n") .. "\nEND\n")
end

-- Active only while the BG3RTX layer runs: it rewrites <bridge dir>/active.txt every
-- second. Without the layer (a normal game start) the bridge does no work.
local hb_last, hb_seen, hb_check = nil, -1e9, -1e9
local function layer_active(now)
  if now - hb_check >= 2.0 then
    hb_check = now
    local s = Ext.IO.LoadFile(DIR .. "active.txt")
    if s ~= nil and s ~= hb_last then hb_last = s; hb_seen = now end
  end
  return now - hb_seen < 5.0
end

Ext.Events.Tick:Subscribe(function()
  if not layer_active(Ext.Timer.MicrosecTime() / 1e6) then return end
  local ok, err = pcall(tick)
  if not ok then Ext.IO.SaveFile(DIR .. "error_client.txt", tostring(err)) end
end)
