-- Actions: buttons in the Mods page for the game's actions. The settings menu stops the game, so each
-- runs when the game goes on; what came of it is printed to the mods' log.
local POTION = 2 -- an item id, as the game counts them

mod.option{
  key = "potion",
  kind = "button",
  label = { en = "Give a potion", es = "Dar una poción" },
  on_change = function() print("give a potion:", game.give_item(POTION, 1), game.item(POTION)) end,
}

mod.option{
  key = "sound",
  kind = "button",
  label = { en = "Play a sound", es = "Reproducir un sonido" },
  on_change = function() print("sound:", game.play_sound("COMMON_SYS", "se_sys_open")) end,
}

mod.option{
  key = "notice",
  kind = "button",
  label = { en = "Show a notice", es = "Mostrar un aviso" },
  description = "The field's notice: the game shows the first one of a field only.",
  on_change = function() print("notice:", game.show_text("Hello from a mod")) end,
}
