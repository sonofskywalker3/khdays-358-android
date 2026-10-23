-- Ten draws and a track of the game's at each mission's start; the field's own music back 20 seconds
-- of the game's time later. The host's draws shared with the guests.
local function ten()
  local t = {}
  for i = 1, 10 do t[i] = random(100) end
  return table.concat(t, " ")
end

mod.on("mission_start", function(e)
  local draws = ten()
  print("mission", e.mission, "draws", draws)
  print("music", music.play("Herc_F"))
  print("share", mod.share({ mission = e.mission, draws = draws }))
  timer.after(20, function() print("20 seconds: music back", music.stop()) end)
end)

mod.on("shared", function(e)
  print("the host's draws", e.data.mission, e.data.draws)
end)
