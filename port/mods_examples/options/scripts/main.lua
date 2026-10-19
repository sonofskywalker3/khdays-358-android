-- Options: one of each kind the Mods page shows, each printing what the player picked.
local speed = mod.option{
  key = "speed",
  kind = "choice",
  label = "Speed",
  values = { "Slow", "Normal", "Fast" },
  default = 2,
  on_change = function(v) print("speed", v) end,
}

mod.option{
  key = "boost",
  kind = "number",
  label = "Boost",
  description = "From 0 to 10, by 2.",
  min = 0,
  max = 10,
  step = 2,
  default = 4,
  on_change = function(v) print("boost", v) end,
}

mod.option{
  key = "hello",
  kind = "button",
  label = { en = "Say hello", es = "Saludar" },
  on_change = function() print("hello; speed is", speed.value) end,
}
