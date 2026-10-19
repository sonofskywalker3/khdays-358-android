-- Where: the mission and the player's position, once a second (30 game frames).
mod.on("frame", function(e)
  if e.frame % 30 ~= 0 then
    return
  end
  local x, y, z = player.position()
  if x ~= nil then
    print(("mission %s at %.2f %.2f %.2f"):format(tostring(mission.id), x, y, z))
  end
end)
