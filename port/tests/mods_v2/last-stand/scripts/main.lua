-- Holds the player's defeat: the hit that would end the run leaves 1 HP and is heard here. After three,
-- the defeat goes back to the game (its game over). Strong enemies round the player hasten them.
print("holding the defeat", player.take_defeat(true))
local defeats, made = 0, false

mod.on("defeat", function(e)
  defeats = defeats + 1
  print("defeat", defeats, "damage", e.damage, "by", tostring(e.enemy), tostring(e.kind), "hp", player.hp)
  if defeats == 3 then
    print("the defeat back to the game", player.take_defeat(false))
  end
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission)
  made = false
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "rank", e.rank, "given up", e.given_up, "ending", e.ending)
end)

mod.on("frame", function()
  if made then
    return
  end
  local k = enemies.kinds()
  if #k > 0 then
    local list, why = enemies.make{ kind = k[1].kind, at = "player", count = 3, spread = 3, strength = 5 }
    print("strong enemies", list and #list or 0, why or "")
    made = true
  end
end)
