-- A load mod: what a gameplay mod usually does each frame -- the player's HP, munny, items, a table
-- of its own -- to measure the scripts' time per frame (KHDAYS_TRACE_MODS).
local seen = {}
local total = 0
mod.on("frame", function(e)
  local hp, max = player.hp, player.max_hp
  if hp ~= nil and hp < max then
    total = total + (max - hp)
  end
  local munny = game.munny or 0
  for id = 0, 63 do
    local n = game.item(id)
    if n ~= nil and n ~= seen[id] then
      seen[id] = n
    end
  end
  local list = {}
  for i = 1, 50 do
    list[i] = (e.frame * i + munny) % 97
  end
  table.sort(list)
  total = total + list[1]
end)
