-- Stop, Stopra and Stopga (628, 629, 630), three of each, given and set as soon as the game lets the deck change: the
-- file loaded at the title, before the room (where a guest's deck is the host's to change), so each player of the
-- room has them. The walks port/tools/regress_walks/stop_room_<n>.input cast them; the port logs the casts and holds
-- ("stop:").
local STOP = {628, 628, 628, 629, 629, 629, 630, 630, 630}
local given = 0

mod.on("frame", function(e)
  if given == #STOP or e.frame % 30 ~= 0 then return end
  local ok, why = deck.give(STOP[given + 1], true)
  if not ok then return end -- "no game" or "not now" yet: another time
  given = given + 1
  print("give", STOP[given], ok, why or "")
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission)
end)

mod.on("enemy_defeated", function(e)
  print("defeated", e.kind or "", mod.frame())
end)
