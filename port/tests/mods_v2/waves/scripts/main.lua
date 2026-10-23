-- Three waves of the area's first kind round the player, each once the last is defeated; before the
-- first, one made and sent away at once (no defeat heard for it). A wave the mission's next area let go
-- is made again there. Every step printed to the mods' log.
local waves = {
  { count = 2, level = 5, strength = 2 },
  { count = 3, hp = 2 },
  { count = 2 },
}
local wave, standing, kind, waiting = 0, {}, nil, 0

local function next_wave()
  local w = waves[wave + 1]
  if w == nil then
    print("all waves defeated")
    return true
  end
  local made, why = enemies.make{ kind = kind, at = "player", count = w.count, spread = 4,
                                  hp = w.hp, level = w.level, strength = w.strength }
  if made == nil then
    print("wave", wave + 1, "not made:", why)
    return false
  end
  wave = wave + 1
  for _, e in ipairs(made) do
    standing[e] = true
  end
  local i = enemies.info(made[1])
  print("wave", wave, #made, why or "", "hp", i.hp, i.max_hp, "level", i.level, "at", i.x, i.y, i.z)
  return true
end

mod.on("hp", function(e)
  if e.who == "player" and e.after < e.before then
    print("player hit", e.before - e.after)
  end
end)

mod.on("enemy_appeared", function(e)
  print("appeared", e.enemy, e.kind)
end)

mod.on("enemy_defeated", function(e)
  print("defeated", tostring(e.enemy), e.kind, e.mine)
  if e.mine and standing[e.enemy] then
    standing[e.enemy] = nil
    if next(standing) == nil then
      waiting = 30
    end
  end
end)

mod.on("frame", function()
  if next(standing) ~= nil and #enemies.list() == 0 then
    -- the mission moved to another area, which let them go: that wave again with the new area's kind
    print("the area let wave", wave, "go")
    kind, wave, standing, waiting = nil, wave - 1, {}, 0
  end
  if kind == nil then
    local k = enemies.kinds()
    if #k > 0 then
      kind = k[1].kind
      print("kinds", #k, k[1].kind, k[1].name)
      waiting = 60
    end
    return
  end
  if waiting > 0 then
    waiting = waiting - 1
    if waiting == 0 then
      if wave == 0 then
        local spare = enemies.make{ kind = kind, at = "player" }
        if spare ~= nil then
          print("spare sent away", enemies.remove(spare[1]))
        end
      end
      if not next_wave() then
        waiting = 30
      end
    end
  end
end)

mod.on("scene", function(e)
  if e.from == 2 then
    kind, wave, standing, waiting = nil, 0, {}, 0
  end
end)
