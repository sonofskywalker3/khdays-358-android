-- A Soldier (kind 4) in an area that makes none, and the player kept within 6 units of where it stood
-- once the battle began (the game places the player as it begins).
local SOLDIER, TURRET, RADIUS = 4, 115, 6
local made, centre, kept = false, nil, 0

local function try(kind)
  local h, why = enemies.make{ kind = kind, at = "player" }
  return h and "made" or why
end

mod.on("frame", function(e)
  if e.paused or #enemies.kinds() == 0 then return end
  local x, y, z = player.position()
  if x == nil then return end
  if not made then
    made = true
    print("soldier", try(SOLDIER), "turret", try(TURRET), "the soldier's kind again", try(SOLDIER))
    return
  end
  if centre == nil then
    local ok, why = player.set_position(x + 3, y, z) -- "not now" until the battle has begun
    if ok then
      centre = { x, z }
      print("moved", string.format("%.1f -> %.1f", x, (player.position())))
    elseif why ~= "not now" then
      print("not moved", why)
      centre = { x, z }
    end
    return
  end
  local dx, dz = x - centre[1], z - centre[2]
  local d = math.sqrt(dx * dx + dz * dz)
  if d > RADIUS then
    player.set_position(centre[1] + dx * RADIUS / d, y, centre[2] + dz * RADIUS / d)
    kept = kept + 1
    if kept == 1 or kept % 60 == 0 then print("kept inside", kept, string.format("%.1f", d)) end
  end
end)

mod.on("enemy_defeated", function(e)
  if e.mine then print("defeated", e.kind) end
end)
