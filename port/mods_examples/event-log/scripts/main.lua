-- Event log: every event but the frame's, with its details, in the mods' log.
local function describe(e)
  local keys = {}
  for k in pairs(e) do
    if k ~= "event" then
      keys[#keys + 1] = k
    end
  end
  table.sort(keys)
  local parts = { e.event }
  for _, k in ipairs(keys) do
    parts[#parts + 1] = k .. "=" .. tostring(e[k])
  end
  return table.concat(parts, " ")
end

for _, name in ipairs({ "scene", "title", "mission_start", "mission_end", "hp", "item", "munny",
                        "menu_open", "menu_close", "save" }) do
  mod.on(name, function(e) print(describe(e)) end)
end
