-- The deck kept, changed for one mission and put back: Level Up (63) and Fire (93) set at Mission
-- Mode's camp before mission file 2 starts; the kept deck restored as the mission ends.
local LEVEL_UP, FIRE = 63, 93
local kept, camps, started, left = nil, 0, nil, false

local function summary(label)
  local d = deck.read()
  local cells = {}
  for _, p in ipairs(d.panels) do cells[#cells + 1] = p.panel .. "@" .. p.page .. "." .. p.row .. "." .. p.col end
  print(label, "open", d.open, "set", table.concat(cells, " "))
end

local function same(a, b)
  for i = 1, #a.grid do
    if a.grid[i] ~= b.grid[i] then return false end
  end
  for id, n in pairs(a.owned) do
    if b.owned[id] ~= n then return false end
  end
  for id, n in pairs(b.owned) do
    if a.owned[id] ~= n then return false end
  end
  return true
end

mod.on("scene", function(e)
  if e.to ~= 19 then return end
  camps = camps + 1
  if camps == 1 then
    kept = deck.save()
    summary("kept")
    print("level up", deck.give(LEVEL_UP, true), "fire", deck.give(FIRE, true))
    summary("changed")
    print("start", mission.start(2))
  elseif camps == 2 then
    summary("back at the camp")
    print("as kept", same(kept, deck.save()))
  end
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission)
  started, left = mod.frame(), false
end)

mod.on("frame", function(e)
  if started ~= nil and not left and e.frame - started >= 600 and e.frame % 30 == 0 and not e.paused then
    local ok, why = mission.leave()
    if ok or why ~= "not now" then
      print("leaving", ok, why, "most HP", player.max_hp)
      left = true
    end
  end
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "ending", e.ending, "restore", deck.restore(kept))
  started = nil
end)
