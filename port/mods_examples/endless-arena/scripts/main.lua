-- Endless arena: an example of a whole game mode written with the mod interface, version 2.
--
-- The title's entry asks the difficulty. In Mission Mode's camp the mod keeps the player's deck, sets a
-- small loadout in it and starts Mission Mode's first mission (file 2). There the mission's own
-- enemies are held back and the mod's waves come instead, growing in number and level, with other
-- missions' kinds from the third; the player is kept inside the arena, a timer runs and the music
-- changes. Between waves the player chooses a reward. When the player falls (the mod holds the
-- defeat) or gives up, a result screen shows the run, the mission is left and the deck put back.
local T = require("texts")

local MISSION = 2                                    -- Mission Mode's first: Twilight Town's square
local LOADOUT = { 63, 63, 63, 93, 93, 105, 2, 2, 2 } -- Level Up x3, Fire x2, Cure, Potion x3
local OTHERS = { 4, 5, 6, 9, 10, 12, 17, 18, 20, 26, 43 } -- other missions' kinds the waves may bring
local PANELS = { 93, 96, 99, 105, 63 }               -- a reward's panel: Fire, Blizzard, Thunder, Cure, Level Up
local RADIUS = 15                                    -- the arena, round where the battle began
local MUSIC = "MissionBoss1"

local difficulty -- 1..3 once chosen at the title; nil when no arena waits
local camp       -- the camp's steps: the deck kept and the loadout set, then the mission asked for
local run        -- the run, from the mission's start to its end

-- ---- the title -------------------------------------------------------------------------------

title.entry{
  label = T.entry,
  on_select = function()
    screen.list{
      title = T.difficulty,
      entries = T.levels,
      on_choose = function(i)
        difficulty = i
        screen.page{ title = T.ready, text = T.how }
      end,
    }
  end,
}

-- ---- the camp: the loadout, then the mission -------------------------------------------------

mod.on("scene", function(e)
  if e.to == 19 and difficulty ~= nil and run == nil and camp == nil then
    camp = { next = 1 }
  end
end)

local function camp_step()
  if camp.kept == nil then
    camp.kept = deck.save()
    if camp.kept == nil then return end
  end
  while camp.next <= #LOADOUT do
    local ok, why = deck.give(LOADOUT[camp.next], true)
    if not ok and why == "not now" then return end -- a page of the camp is open: next frame
    camp.next = camp.next + 1                       -- set, or no room: the run goes on without it
  end
  local ok, why = mission.start(MISSION)
  if ok then
    camp.asked = true
  elseif why ~= "not now" then
    print("the arena cannot start", why)
    deck.restore(camp.kept)
    camp, difficulty = nil, nil
  end
end

-- ---- the run ---------------------------------------------------------------------------------

local function clock(seconds)
  return ("%d:%02d"):format(seconds // 60, seconds % 60)
end

local function show_result()
  local this = run
  screen.result{
    title = T.result,
    rows = {
      { T.difficulty, T.levels[run.difficulty] },
      { T.waves, run.state == "break" and run.wave or run.wave - 1 }, -- a wave is cleared as its break begins
      { T.defeated, run.defeated },
      { T.time, clock(run.seconds) },
    },
    on_close = function() this.leaving = run == this end, -- left already when the mission has ended
  }
end

-- The run over: its enemies sent away, the game's own back, the result shown.
local function finish()
  if run.over then return end
  run.over = true
  enemies.remove(enemies.list())
  enemies.hold_mission(false)
  player.take_defeat(false)
  music.stop()
  timer.cancel(run.clock)
  show_result()
end

local function wave_kinds()
  local kinds = {}
  for _, k in ipairs(enemies.kinds()) do kinds[#kinds + 1] = k.kind end
  if run.wave >= 3 then
    local all, ready = enemies.kinds("all"), {}
    for _, kind in ipairs(OTHERS) do
      if all[kind + 1] and all[kind + 1].ready then ready[#ready + 1] = kind end
    end
    if #ready > 0 then kinds[#kinds + 1] = ready[random(#ready)] end
  end
  return kinds
end

local function next_wave()
  local kinds = wave_kinds()
  local counts, total = {}, run.wave + run.difficulty
  if #kinds == 0 then return false end
  for _ = 1, total do
    local kind = kinds[random(#kinds)]
    counts[kind] = (counts[kind] or 0) + 1
  end
  for kind, count in pairs(counts) do
    local made, why = enemies.make{
      kind = kind, at = "player", count = count, spread = 6,
      level = (run.wave - 1) + (run.difficulty - 1) * 3,
      hp = 1 + 0.1 * (run.wave - 1),
      strength = 0.7 + 0.15 * run.difficulty + 0.04 * (run.wave - 1),
    }
    if made == nil and why == "not in multiplayer yet" then
      local this = run
      screen.page{ title = T.entry, text = T.alone, on_close = function() this.leaving = run == this end }
      run.over = true
      return true
    end
  end
  run.state = "fighting"
  return true
end

local function reward()
  screen.list{
    title = T.reward,
    entries = { T.rewards.heal, T.rewards.munny, T.rewards.panel },
    on_choose = function(i)
      if i == 1 then
        player.set_hp(player.max_hp or 1)
      elseif i == 2 then
        game.set_munny(game.munny + 500)
      else
        local panel = PANELS[random(#PANELS)]
        if deck.give(panel) then run.panels[#run.panels + 1] = panel end
      end
      run.wave = run.wave + 1
      run.state = "next"
    end,
    on_cancel = function() run.wave = run.wave + 1; run.state = "next" end,
  }
end

mod.on("mission_start", function()
  if camp == nil or not camp.asked then return end
  run = { difficulty = difficulty, kept = camp.kept, wave = 1, defeated = 0, seconds = 0, panels = {},
          state = "waiting" }
  camp = nil
  enemies.hold_mission(true)
  player.take_defeat(true)
  music.play(MUSIC)
  run.clock = timer.every(1, function() run.seconds = run.seconds + 1 end)
end)

mod.on("frame", function(e)
  if camp ~= nil and not camp.asked then camp_step() end
  if run == nil or e.paused then return end
  if run.leaving then
    local ok, why = mission.leave()
    if ok or why == "not in the field" then run.leaving = false end
    return
  end
  if run.over then return end
  local x, y, z = player.position()
  if x == nil then return end
  if run.state == "waiting" then
    -- the area's enemies made and the battle begun: the arena is where the player stands then
    if #enemies.kinds() > 0 and player.set_position(x, y, z) then
      run.centre = { x, z }
      run.state = "next"
    end
    return
  end
  local dx, dz = x - run.centre[1], z - run.centre[2]
  local d = math.sqrt(dx * dx + dz * dz)
  if d > RADIUS then
    player.set_position(run.centre[1] + dx * RADIUS / d, y, run.centre[2] + dz * RADIUS / d)
  end
  if run.state == "next" then
    next_wave()
  elseif run.state == "fighting" and #enemies.list() == 0 then
    run.state = "break"
    timer.after(2, reward)
  end
end)

mod.on("enemy_defeated", function(e)
  if run ~= nil and e.mine then run.defeated = run.defeated + 1 end
end)

mod.on("defeat", function()
  if run ~= nil then finish() end
end)

mod.on("mission_end", function()
  if run == nil then return end
  if not run.over then finish() end -- given up from the pause
  deck.restore(run.kept)
  for _, panel in ipairs(run.panels) do deck.give(panel) end -- the rewards stay the player's
  run, difficulty = nil, nil
end)
