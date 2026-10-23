-- Leaves the mission 600 frames after it starts, and logs how it ended.
local started, left

mod.on("mission_start", function(e)
  print("mission start", e.mission)
  started, left = mod.frame(), false
end)

mod.on("frame", function(e)
  if started ~= nil and not left and e.frame - started >= 600 and not e.paused then
    print("leaving", mission.leave())
    left = true
  end
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "rank", e.rank, "given up", e.given_up, "ending", e.ending)
  started = nil
end)
