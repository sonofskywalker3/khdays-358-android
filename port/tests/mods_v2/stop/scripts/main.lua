-- Stop, Stopra and Stopga (628, 629, 630) given and set at Mission Mode's camp -- Stopra on a cell of its own, Stop on
-- a Doublecast's cell (two casts), Stopga on a Magic LV2's (a longer hold) -- then mission file 2, its own enemies
-- held back, with three of the area's enemies made four units from the player just after the walk presses its first
-- cast (port/tools/regress_walks/stop.input: Stopga, then Stopra and Stop while the enemies are held, then attacks
-- whose damage they keep): they come standing and strike at once, so the cast takes them before their first blow
-- (a blow on Roxas would swallow the submenu's A); the port logs each hold ("stop:" lines), this mod the mission, the
-- enemies made and the defeats.
local STOP, STOPRA, STOPGA = 628, 629, 630
local MAGIC_LV2, DOUBLECAST = 108, 114
local MAKE_AT = 3700 -- the mod's frame just after the walk presses its first cast (the walk's 7390)
local camps, kind, made = 0, nil, false

local function summary(label)
  local d = deck.read()
  local cells = {}
  for _, p in ipairs(d.panels) do cells[#cells + 1] = p.panel .. "@" .. p.page .. "." .. p.row .. "." .. p.col end
  print(label, "open", d.open, "set", table.concat(cells, " "))
end

-- Panel `id` given and set on a cell of multi-cell panel `host` (set already) other than its own.
local function on_host(id, host)
  local at
  for _, p in ipairs(deck.read().panels) do
    if p.panel == host then at = p end
  end
  for _, c in ipairs(at and deck.info(host).cells or {}) do
    if c.row ~= 0 or c.col ~= 0 then
      print("give", id, "on", host, deck.give(id, { page = at.page, row = at.row + c.row, col = at.col + c.col }))
      return
    end
  end
  print("give", id, "on", host, "no cell")
end

mod.on("scene", function(e)
  if e.to ~= 19 then return end
  camps = camps + 1
  if camps ~= 1 then return end
  for _, id in ipairs({STOP, STOPRA, STOPGA}) do
    local info, why = deck.info(id)
    print("panel", id, info and info.category or why, info and info.kind or "")
  end
  print("give", STOPRA, deck.give(STOPRA, true))
  print("give", DOUBLECAST, deck.give(DOUBLECAST, true))
  print("give", MAGIC_LV2, deck.give(MAGIC_LV2, true))
  on_host(STOP, DOUBLECAST)
  on_host(STOPGA, MAGIC_LV2)
  summary("deck")
  print("start", mission.start(2))
end)

mod.on("mission_start", function(e)
  print("mission start", e.mission, "its enemies held", enemies.hold_mission(true))
end)

mod.on("enemy_defeated", function(e)
  print("defeated", e.kind or "", mod.frame())
end)

mod.on("mission_end", function(e)
  print("mission end", e.mission, "ending", e.ending)
end)

mod.on("frame", function(e)
  if made or e.paused then return end
  if kind == nil then
    local kinds = enemies.kinds()
    if #kinds > 0 then kind = kinds[1].kind end
    return
  end
  if e.frame < MAKE_AT then return end
  local x, y, z = player.position()
  local m, why = enemies.make{ kind = kind, at = { x + 4, y, z }, count = 3, spread = 1 }
  made = true
  print("made", m and #m or 0, why or "")
end)
