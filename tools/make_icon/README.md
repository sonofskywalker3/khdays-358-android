# Icon generator

Draws the port's icons from a vector crown mark, so every size is generated rather than
stored by hand. Where the icons come from is in `ART-SOURCES.md` at the repository's root.

It writes:

- the Android launcher set, for each of `mdpi` through `xxxhdpi` in `android/src/main/res/`:
  `ic_launcher.png` (rounded tile), `ic_launcher_round.png` (circle) and
  `ic_launcher_foreground.png` (the adaptive icon's transparent front layer; the back layer is
  the colour in `values/ic_launcher_background.xml`);
- `port/resources/game.ico` (the game and its installer) and `studio/resources/studio.ico`, each
  with 16 to 256 px images drawn one by one.

## Usage

From the repository's root, with Pillow installed:

```powershell
py .\tools\make_icon\make_icon.py
```

`--res-dir` writes the Android set somewhere else. The silhouette is `CROWN_PATH`, in normalised
coordinates; `LEGACY_SCALE` and `ADAPTIVE_SCALE` set how much of each tile it fills, and the
adaptive value keeps the points inside the part of the icon every launcher mask shows.
