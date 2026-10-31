-- One-shot READ-ONLY game-state probe. No component writes, subscriptions or timers.
-- Run in client context through SE console; report files are the only writes.
do
  local nonce = "__BG3RTX_PROBE_NONCE__"
  local clock = Ext.Timer.MicrosecTime
  local started = clock()
  local report = {schema = 1, nonce = nonce, source = "native LightComponent reads", lights = {}, errors = {}}
  local function read(fn)
    local ok, value = pcall(fn)
    if not ok then return {status = "error", error = tostring(value):sub(1,512)} end
    if value == nil then return {status = "unknown_nil"} end
    return {status = "ok", value = value}
  end
  report.se_version = read(function() return Ext.Utils.Version() end)
  report.game_version = read(function() return Ext.Utils.GameVersion() end)
  local function v3(v) return {v[1], v[2], v[3]} end
  local function vec(light, field) return read(function() return v3(light[field]) end) end
  local function scalar(light, field) return read(function() return light[field] end) end
  local function text(light, field) return read(function() return tostring(light[field]) end) end
  local level
  for _, e in pairs(Ext.Entity.GetAllEntitiesWithComponent("ClientControl")) do
    if e.ClientCharacter and e.ClientCharacter.ReservedUserID == 1 then
      level = e.Level and e.Level.LevelName
      break
    end
  end
  report.level = level or "UNKNOWN"
  -- Fixed authored/native-qualified S1 cell, not guessed selection or light activation.
  local lo = {-336.5700150735474, 2.6991598799228655, -150.79098184073968}
  local hi = {-327.563724328416, 19.481098307757446, -138.71733325453377}
  report.bounds = {lo, hi}
  local all = Ext.Entity.GetAllEntitiesWithComponent("Light")
  report.all_light_entities = #all
  report.candidate_count = 0
  report.truncated = false
  if level == "CTY_Main_A" then
    for _, entity in ipairs(all) do
      local light = entity.Light
      local ok, selected = pcall(function()
        if tostring(light.LightType) ~= "Point" then return false end
        local p, radius = light.WorldTransform.Translate, light.Radius
        if type(radius) ~= "number" or radius ~= radius or radius <= 0 or radius == math.huge then return false end
        local d2 = 0
        for j = 1, 3 do
          local d = math.max(lo[j] - p[j], 0, p[j] - hi[j])
          d2 = d2 + d * d
        end
        return d2 <= radius * radius
      end)
      if not ok then
        if #report.errors < 8 then report.errors[#report.errors + 1] = tostring(selected):sub(1,512) end
      elseif selected then
        report.candidate_count = report.candidate_count + 1
        if #report.lights < 32 then
          local row = {}
          row.entity_id = read(function() return tostring(Ext.Utils.HandleToInteger(entity)) end)
          row.component_type = read(function() return Ext.Types.GetObjectType(light) end)
          row.blackbody_reflected_type = read(function() return tostring(Ext.Types.TypeOf(light).Members.Blackbody.TypeName) end)
          row.uuid = text(light, "UUID")
          row.position = read(function() return v3(light.WorldTransform.Translate) end)
          row.Color, row.Blackbody = vec(light, "Color"), vec(light, "Blackbody")
          row.PositionOffset = vec(light, "PositionOffset")
          for _, name in ipairs({"Intensity", "IntensityOffset", "Gain", "Kelvin", "Radius", "EdgeSharpening", "LightChannelFlag", "CullFlags", "OwnerFlags"}) do
            row[name] = scalar(light, name)
          end
          row.Flags, row.LightType = text(light, "Flags"), text(light, "LightType")
          row.template_name = read(function() return light.Template and tostring(light.Template.Name) end)
          row.template_file = read(function() return light.Template and tostring(light.Template.FileName) end)
          row.template_flat_falloff = read(function() return light.Template and light.Template.FlatFalloff end)
          row.template_pre_expose = read(function() return light.Template and light.Template.PreExpose end)
          report.lights[#report.lights + 1] = row
        else
          report.truncated = true
        end
      end
    end
    report.status = report.truncated and "truncated" or "ok"
  else
    report.status = "wrong_or_unknown_level"
  end
  report.probe_us = clock() - started
  report.output = "bg3rtx/native_blackbody_probe_" .. nonce .. ".json"
  local encoded = Ext.Json.Stringify(report)
  if not Ext.IO.SaveFile(report.output, encoded) then error("BG3RTX read-only probe report write failed") end
  print("[BG3RTX_SE_PROBE] " .. report.status .. " records=" .. #report.lights .. " nonce=" .. nonce)
end
