# Art sources

Where each piece of artwork in this repository comes from and how it was made. The game's own
art is never stored here: the installer and the tools build it on your machine from your own ROM
and, for the HD pieces, your own copy of the HD collections.

## The icons

| Files | What it is | How it was made |
| --- | --- | --- |
| `android/src/main/res/mipmap-*/ic_launcher*.png`, `android/src/main/res/values/ic_launcher_background.xml` | Android launcher icon (plain, round and adaptive) | A crown drawn as vector code by `tools/make_icon/make_icon.py`, a drawing script written by generative AI (Claude, Anthropic) |
| `port/resources/game.ico` | The game's Windows icon, also used by the installer | The same crown, written by the same script |
| `studio/resources/studio.ico` | The mod studio's Windows icon | The same crown, written by the same script |

The upstream project ([Yokimitsuro/khdays-port](https://github.com/Yokimitsuro/khdays-port)) uses
icons built on Kingdom Hearts' own logo and symbols. They look great, and this fork used them for a
while. I decided to ship only icons I can fully account for, since the logo and symbols belong to
the game's owners and no open-source licence can cover them, so this fork draws its own crown.
That is a choice about what I am comfortable shipping, not a judgement on upstream.

## Other art

| Files | What it is | Where it comes from |
| --- | --- | --- |
| `android/src/main/assets/touch/` | The on-screen controls | Kenney's Mobile Controls (CC0), unchanged; see `THIRD_PARTY_NOTICES.md` |
| `port/resources/input_prompts/` | Controller and keyboard button pictures | Kenney's Input Prompts (CC0), unchanged; see `THIRD_PARTY_NOTICES.md` |
| `tools/mods/overhaul/art/` | The Menu Overhaul's own pictures (Files symbols, shop and camp pieces) | Drawn for the mod by its author, Filipe Garcia, in the upstream project |
| `sdk/examples/*/characters/`, `port/tests/mods_v2/` | Placeholder textures for the SDK examples and tests | Made by Yokimitsuro in the upstream project |

## Replacements welcome

I would rather use art made by people. If you would like to draw a replacement for any piece made
with AI above, open an issue or a pull request: it will be used with full credit to you, under the
licence you choose.
