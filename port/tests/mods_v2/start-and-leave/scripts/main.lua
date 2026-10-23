-- At Mission Mode's camp, mission file 2 started once; left 600 frames into it, once its battle is up.
local asked, started, left = false, nil, false

mod.on("scene", function(e)
  if e.to == 19 and not asked then
    asked = true
    print("start", mission.start(2))
  end
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission, "kinds", #enemies.kinds())
  started, left = mod.frame(), false
end)

mod.on("frame", function(e)
  if started ~= nil and not left and e.frame - started >= 600 and e.frame % 30 == 0 and not e.paused then
    local ok, why = mission.leave() -- "not now" while the mission's opening plays
    if ok then
      local k = enemies.kinds()
      print("left after", e.frame - started, "frames; the area's kinds", #k, k[1] and k[1].name or "")
      left = true
    elseif why ~= "not now" then
      print("leaving", ok, why)
      left = true
    end
  end
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "ending", e.ending)
  started = nil
end)
