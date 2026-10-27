-- BG3RTX bridge, server context (docs/SE_BRIDGE.md).
--
-- Writes %LOCALAPPDATA%\Larian Studios\Baldur's Gate 3\Script Extender\bg3rtx\world.txt
-- when it changes (checked once a second). Format v1, text:
--   BG3RTX_SE_WORLD 1
--   level <level> region <Osiris region>
--   T <kind> <trigger name> <current resource id or -> <trigger uuid>
--   END
-- kind: A atmosphere trigger, L lighting trigger, of the current level only. The current
-- resource is what story scripting switched the trigger to (CurrentAtmosphereResourceID /
-- CurrentLightingResourceID). Which trigger contains the camera is left to the layer
-- (the asset database has the trigger shapes, by name). BG3 has no time of day.

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
local OUT = DIR .. "world.txt"
local PERIOD_US = 1000000
local clock = Ext.Timer.MicrosecTime
local last, written = 0, ""
local function try(f, ...) local ok, v = pcall(f, ...); if ok then return v end end

local function triggers(comp, kind, field, level, out)
  for _, e in ipairs(Ext.Entity.GetAllEntitiesWithComponent(comp)) do
    local t = e[comp]
    if t and t.Level == level then
      local tpl = t.Template
      local cur = t[field]
      if cur == nil or cur == "" then cur = "-" end
      out[#out + 1] = string.format("T %s %s %s %s", kind, tpl and tpl.Name or "-", cur, tpl and tpl.Id or "-")
    end
  end
end

local function tick()
  local now = clock()
  if now - last < PERIOD_US then return end
  last = now
  local hc = try(function() return Osi.GetHostCharacter() end)
  if not hc then return end
  local h = Ext.Entity.Get(hc)
  local level = h and h.Level and h.Level.LevelName or "-"
  local region = try(function() return Osi.GetRegion(hc) end) or "-"
  local out = {"BG3RTX_SE_WORLD 1", string.format("level %s region %s", level, region)}
  triggers("ServerAtmosphereTrigger", "A", "CurrentAtmosphereResourceID", level, out)
  triggers("ServerLightingTrigger", "L", "CurrentLightingResourceID", level, out)
  out[#out + 1] = "END"
  local s = table.concat(out, "\n") .. "\n"
  if s ~= written then
    Ext.IO.SaveFile(OUT, s)
    written = s
  end
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
  if not ok then Ext.IO.SaveFile(DIR .. "error_server.txt", tostring(err)) end
end)
