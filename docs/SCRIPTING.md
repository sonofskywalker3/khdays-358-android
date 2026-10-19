# Scripting gameplay mods

A mod can change how the game plays with scripts in Lua 5.4. The port runs each scripted mod in its
own sandboxed interpreter, on the game's thread, once per game frame and when the game does something
a script listens to. Scripts read and change the game's state by name, never by address: the names
below stay the same across port updates of the same interface version.

Content mods (textures, models, 2D, sounds, texts, movies, rooms) are unchanged by this; a scripted
mod may carry content too.

## A mod's folder

```text
mods/<folder>/
├── mod.ini              # what makes it a scripted mod
├── scripts/
│   ├── main.lua         # run once when the mod is loaded
│   └── <name>.lua       # require("name"); require("a.b") is scripts/a/b.lua
├── data/                # text files the mod reads (optional)
└── ...                  # content, as any mod (optional)
```

`mod.ini`, `key = value` lines (`#` starts a comment):

```ini
id = example.infinite-hp
name = Infinite HP
version = 1.0
author = khdays-port
kind = gameplay
api = 1
description = Keeps the player's HP full.
```

| Key | | |
|---|---|---|
| `id` | required | letters, digits, `.`, `_`, `-`; no two mods may share one |
| `name`, `version` | required | shown to the player; versions are compared as text |
| `kind` | required | `gameplay` (it changes the game) or `visual` (it only listens: its changes are refused) |
| `api` | required | the interface version it needs: this port gives `1` |
| `main` | optional | the first script, under `scripts/` (default `scripts/main.lua`) |
| `author`, `description` | optional | shown to the player |

A folder whose `mod.ini` is missing a required key, asks for a newer interface, repeats another mod's
id or has no main script is not loaded, and the reason is logged.

Mods load in the order kept in `saves/mods/order.ini` (`<folder> = on|off`, one line each); a mod
not listed yet comes after the others, on. Events reach the mods in that order: when two mods change
the same thing in a frame, the later one's change stands.

## What a script may use

`string`, `table`, `math`, `utf8`, `coroutine`, the base functions (`load` of text only, without
`dofile` and `loadfile`), `require` of the mod's own scripts, and `os.time`, `os.clock`, `os.date`,
`os.difftime`. Nothing else: no files beyond the mod's own, no programs, no native code, no network.
Metatables cannot have `__gc`.

Each mod may hold 64 MB; past that, an allocation fails (`not enough memory`). Each mod has a time
allowance per game frame (8 ms; its main script has 250 ms when it loads). A mod past its allowance
is paused, even inside its own `pcall`; an error in a script stops that mod only. Either is written
to the mods' log with the script's file and line, the player sees it over the game for five seconds
(and on the settings menu's notice line), and the game and the other mods go on. The Mods page
resumes a paused mod.

For a measure: ten mods each reading the player's HP, munny and 64 item counts and sorting a table
every frame (`port/tests/mods_load`) took 45 microseconds each a frame on average, 0.2 ms at most,
and the game kept its 30 frames a second.

A known limit: the time is looked at between the script's own instructions, so a single library call
that takes long (a string pattern over a very long text) is only stopped once it returns.

## The mods' log

`saves/logs/mods.log` holds a line per thing each mod printed or met (the last run's is
`mods.prev.log`):

```text
12.345 example.infinite-hp loaded - started, api 1
13.002 example.where print scripts/main.lua:8 mission 10 at 12.50 0.00 -3.25
20.410 example.broken error scripts/main.lua:3 attempt to index a nil value (global 'plyer')
20.410 example.broken stopped - the error above
```

The same lines go to the session log after `mods:`.

## Trying a change while the game runs

The game looks at each mod's files once a second: when `mod.ini` or a script changed, that mod is
loaded again (its state closed, its main script run again; its options' values and its own files
are kept). A stopped mod comes back the same way once its script is fixed. khdays-studio's Console
(the button beside Themes) follows `mods.log` as it grows, by mod and with errors only, and its
Reload asks the game to load a mod again (a line in `saves/mods/reload.txt`, read within a second).

## The interface, version 1

Calls that change the game return `true`, or `nil` and a reason (`"visual mod"`, `"guest"` in a
multiplayer session that the player does not host, `"no game"` before a game is started or loaded,
or the call's own); they never raise. A wrong argument type raises an error (the mod's own bug).

### `mod`

| Name | |
|---|---|
| `mod.id`, `mod.name`, `mod.version` | from `mod.ini` |
| `mod.on(event, function(e) ... end)` | listens to an event (below); returns a handle |
| `mod.off(handle)` | stops listening |
| `mod.frame()` | the game's frame count |
| `mod.notice(text)` | a line the player sees over the game for five seconds, with the mod's name |
| `mod.read(path)` | a text file of the mod's: its `data/<path>`, else what it wrote; `nil` and a reason if neither |
| `mod.write(path, text)` | keeps a text file in `saves/mods/<id>/<path>` (its folders made); paths stay inside: no `..`, no absolute path, no drive, no backslash |
| `print(...)` | a line in the mods' log |
| `mod.option{...}` | an option in the settings menu's Mods page (below) |

### Events

Each handler gets one table, `e`, with `e.event` its name and the fields below.

| Event | Fields | When |
|---|---|---|
| `frame` | `frame`, `paused` | every game frame, once the game's logic of the frame is done (the pause included: `paused` is true while the game's objects stand still) |
| `scene` | `from`, `to`, `argument` | the game changed scenes |
| `title` | | the title scene was entered |
| `mission_start` | `mission` | the field began: a mission (again after Retry), or in Story Mode the Grey Area, where `mission` is whatever the game holds then |
| `mission_end` | `mission`, `rank`, `given_up`, `complete` | the mission's results were recorded (after a give-up too: `given_up` true; a give-up from Mission Mode's pause had `rank` -1); `complete` only in Story Mode |
| `hp` | `who` (`"player"` or `"other"`), `before`, `after`, `max` | an HP changed in battle (`after < before`: damage) |
| `item` | `item`, `count`, `change` | an item's count changed (picked up, bought, used, given) |
| `munny` | `total`, `change` | the munny changed |
| `menu_open`, `menu_close` | `menu` (`"pause"`, `"camp"`, `"settings"`) | the pause, the camp or the port's settings menu |
| `save` | | the game saved |

What a handler changes is seen by the game's next frame. An event the game raises while a script runs
(a change a script made itself) reaches the mods once that script has returned.

### The game's state

| Name | | Limits |
|---|---|---|
| `game.munny`, `game.set_munny(n)` | munny | 0 to 999999 |
| `game.item(id)`, `game.set_item(id, n)` | an item's count | ids 0 to 2255 (`0x8cf`), counts 0 to 99; a count above 0 marks the item owned |
| `game.flag(n)`, `game.set_flag(n, on)` | a story or mission flag | 0 to 16383 (`0x3fff`) |
| `player.hp`, `player.max_hp`, `player.set_hp(n)` | the player's HP in the field | `set_hp` clamps to 0..`max_hp`, as the game's own hits and cures do (0 is a defeat); `nil` / `nil, "no player"` elsewhere |
| `player.position()` | `x, y, z` in the field | `nil, "no player"` elsewhere |
| `mission.id` | the current mission | `nil` outside the field |
| `scene.id` | the current scene | |

### Actions

| Name | Does | Refused |
|---|---|---|
| `game.give_item(id, n)`, `game.take_item(id, n)` | adds or takes `n` (default 1) of an item, its count kept within 0..99, as the game's rewards do | `"unknown item"`, `"no game"` |
| `game.play_sound(archive, entry)` | plays a sequence archive's entry by its names in the sound archive (`"COMMON_SYS"`, `"se_sys_open"`; the session log names each sound the game plays as `seqarc/<archive>/<entry>`) | `"unknown sound"` |
| `game.show_text(text)` | the field's notice (`Ov002_ShowNoticeText`); the game shows the first notice of a field only, so a mission that has shown its own refuses it | `"not in the field"`, `"busy"`, `"not a text the game can show"` (control characters, characters beyond the first Unicode plane) |

A visual mod and a guest may play sounds; the other actions are refused to them. Whether the game's
font has every character of a notice is not checked.

### Options in the Mods page

The settings menu has a Mods page while a scripted mod is present: each mod with its state (running,
off, stopped by an error, paused, not loaded and why), a switch to turn it on or off, its place in the
load order, Resume for a paused mod, and its options while it runs. The pause and the camp have no
mod entries; the settings menu opens from anywhere with its own key.

```lua
local on = mod.option{ key = "on", kind = "toggle", default = true,
                       label = { en = "Infinite HP", es = "PV infinitos" },
                       description = "The player's HP back to its maximum every frame.",
                       on_change = function(v) print("now", v) end }
mod.option{ key = "speed", kind = "choice", label = "Speed", values = { "Slow", "Normal", "Fast" }, default = 2 }
mod.option{ key = "boost", kind = "number", label = "Boost", min = 0, max = 10, step = 2, default = 4 }
mod.option{ key = "hello", kind = "button", label = "Say hello", on_change = function() print("hello") end }
-- on.value: true or false, as the player left it
```

| Field | |
|---|---|
| `key` | the name its value is kept under; one per mod |
| `kind` | `toggle` (true / false), `choice` (the value's place in `values`, from 1), `number` (whole `min`, `max`, `step`), `button` |
| `label`, `description` | a text, or a table by language (`en`, `fr`, `de`, `it`, `es`); English is the fallback |
| `default` | the value until the player picks one |
| `on_change` | called with the new value when the player changes it (a button: when pressed) |

`mod.option` returns the option: `option.value` is its value now, `option:set(v)` changes it from the
script (kept like the player's change; `on_change` is not called). The values are kept in
`saves/mods/<id>/options.ini` and are back at the next start; a kept value the option no longer
takes goes back to its default. What the player changes in the menu reaches the mod when the game
runs again.

### Not in version 1

What the decompilation does not explain yet, or what was not yet seen working in the game, is left
out rather than guessed: warping and the field's current area, the deck and abilities, starting a
mission (`Ov025_MissionList_StartMission` needs the story camp's list on screen: not yet tried in
play), making a mission's enemies (`Ov002_CreateActorFromMarker` needs one of its spawners: none was
there whenever it was tried), who dealt a hit, a loaded save's event, the mission's chests, telling
a cleared mission from a lost one.

## Multiplayer

The host's game decides for everyone in a room, so a room takes only players with the same gameplay
mods as its host: the same ids, versions and scripts (a fingerprint of them travels with the request
to join). A player with other gameplay mods is not let in and the room list says which mods the room
plays with. Visual mods are each player's own and never keep anyone out.

In a room, a guest's scripts still run and hear the events, but what they would change in the game
is refused (`nil, "guest"`): the host's mods make those changes, with the host's option values, and
they reach the guests as the rest of the game does. A guest's gameplay mods' options are shown locked
in the Mods page. A mod turned on, off or moved while in a room counts from the next room.

## Examples

`port/mods_examples/`: copy a folder into `mods/` to try it.

| Mod | Shows |
|---|---|
| `event-log` | every event, in the mods' log |
| `where` | the mission and the player's position, once a second |
| `double-munny` | the `munny` event and `game.set_munny` |
| `infinite-hp` | `frame`, the player's HP and a toggle |
| `options` | a choice, a number and a button in the Mods page |
| `actions` | Mods-page buttons giving a potion, playing a sound and showing the field's notice |
