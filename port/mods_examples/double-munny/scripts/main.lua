-- Double munny: whatever munny the player gains is given again. The extra munny raises its own event
-- next frame; `giving` keeps it from being doubled in turn.
local giving = 0

mod.on("munny", function(e)
  if e.change <= 0 then
    return
  end
  if giving > 0 then
    giving = giving - e.change
    return
  end
  giving = e.change
  game.set_munny(e.total + e.change)
  print(("munny +%d, given again: %d"):format(e.change, game.munny))
end)
