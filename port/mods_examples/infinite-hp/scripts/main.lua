-- Infinite HP: on every frame, the player's HP back to its maximum, while its option is on.
local on = mod.option{
  key = "on",
  kind = "toggle",
  label = { en = "Infinite HP", fr = "PV infinis", de = "Unendliche LP", it = "PV infiniti", es = "PV infinitos" },
  description = "The player's HP goes back to its maximum every frame.",
  default = true,
}

mod.on("frame", function()
  if not on.value then
    return
  end
  local hp, max = player.hp, player.max_hp
  if hp ~= nil and hp < max then
    player.set_hp(max)
  end
end)
