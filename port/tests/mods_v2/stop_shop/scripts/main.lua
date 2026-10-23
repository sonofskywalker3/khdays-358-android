-- Stop's first recipe asks for two Frost Gems (19) and two Lightning Gems (23): given at Mission Mode's camp, they
-- list the recipe at the moogle (port/tools/regress_walks/stop_shop.input synthesises it); the port logs the recipes
-- added ("stop:"), this mod the gems, the platines and Stop (628) once it is owned.
local STOP, GEMS = 628, {19, 23}
local camps, owned = 0, false

mod.on("scene", function(e)
  if e.to ~= 19 then return end
  camps = camps + 1
  if camps ~= 1 then return end
  for _, id in ipairs(GEMS) do print("give", id, game.give_item(id, 2), game.item(id)) end
  print("Stop owned", game.item(STOP))
end)

mod.on("frame", function()
  if owned or camps == 0 or game.item(STOP) == 0 then return end
  owned = true
  print("Stop synthesised", game.item(STOP))
end)
