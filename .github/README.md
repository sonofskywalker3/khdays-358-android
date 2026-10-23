# 358/2 Days for Android

https://github.com/user-attachments/assets/cf4f4c99-220c-4a11-9ce0-7cd8a10f9d3b

*Running on the AYN Thor: both screens, HD 3D and the HD cutscenes.*

**KINGDOM HEARTS 358/2 Days on Android, built on
[Yokimitsuro's khdays-port](https://github.com/Yokimitsuro/khdays-port).** The port itself, and the
[decompilation](https://github.com/Yokimitsuro/khdays-decomp) it runs on, come from there; all the
credit for the game running natively goes to that project. This fork adds the Android app and the
AYN Thor's two-screen layout, and follows upstream's releases. **For the PC version, go to the
[original repository](https://github.com/Yokimitsuro/khdays-port).**

[![Discord](https://img.shields.io/badge/Discord-Join%20the%20community-5865F2?logo=discord&logoColor=white)](https://discord.gg/rVeTPhPbsx)

> [!IMPORTANT]
> This repository contains no ROM, game assets, audio, video, text, SDK files or game binaries. You
> need your own legally obtained copy of the game.

> [!WARNING]
> This is a beta and may have bugs. See [Reporting problems](#reporting-problems).

## What you get

- The whole game, natively: Story Mode, Mission Mode, combat and magic, the shop, saving and loading.
  The game is the decompilation's C compiled for ARM, not an emulator.
- **Two screens on the AYN Thor**: the upper screen on the main display, the lower one on the second
  display by itself, with touch. On a phone or a one-screen handheld, both screens share the display.
- Sharp 3D at up to the display's resolution, with widescreen and smoother motion in the settings.
- Optional **HD cutscenes and HD menus** made from your own copies of the HD collections.
- Everything upstream's port adds to the game, such as the restored Stop magic.
- The European and the North American release.
- Updates from inside the game (from beta 4 on).

## Requirements

- A 64-bit ARM device with Vulkan and Android 10 or later. It is built and tested on the AYN Thor.
- About 400 MB of free space for the game, and several GB more for the optional HD cutscenes.
- The `.nds` ROM of your own KINGDOM HEARTS 358/2 Days cartridge (European or North American).

## Installing

1. Download the APK from the [releases page](https://github.com/sonofskywalker3/khdays-358-android/releases)
   and open it on the device to install it (allow installs from your browser or file manager when
   Android asks).
2. Copy your ROM onto the device with a USB cable or an SD card, into any folder.
3. Open **358/2 Days**. The setup screen asks for the ROM: press **Choose ROM file** and pick it. It is
   checked and unpacked; then press **Play**. From then on the app opens straight into the game.

The setup screen works by touch, or with the D-pad and A (B goes back).

### HD cutscenes and menus (optional)

They are made on a Windows PC from your own copies of the HD collections, then copied over:

1. On a Windows PC with KINGDOM HEARTS HD 1.5+2.5 ReMIX (Steam or Epic), run the PC installer from the
   releases page. When it asks for your ROM, give it your `.nds` file. Tick **HD videos**, and **HD
   menus** too if you also have KINGDOM HEARTS HD 2.8 Final Chapter Prologue.
2. When it is done, copy the whole folder it installed to onto the device, into any folder (such as
   Download).
3. On the device, long-press the app's icon and choose **Set up / add HD files**, then **Choose the
   copied folder** and pick it. The app copies the HD files in and shows what was added; they are on
   the next time you play. It then offers to delete the copy you made, to get the space back.

## Playing

The device's own controls play the game as on the DS, by position: the right face button is A, the
bottom one B, the left one Y and the top one X; the shoulders are L and R. On the Thor, touch the
lower display where the DS's touch screen is used.

**Settings**: press **L3** (click the left stick in). The menu works with the controls alone: L and R
move between its sections. It has the PC port's options, such as the controls, an analog camera on the
right stick, analog movement, the video and sound settings and the language. Upstream's
[README](https://github.com/Yokimitsuro/khdays-port#controls) describes them all.

## Saves and updating

The app keeps the game, the HD files and your saves in
`Android/data/com.sonofskywalker3.khdays358/files` (saves in `saves/`). The save is a standard DS save
file, so a save from the DS or an emulator can be copied in as `saves/khdays.sav`.

- **Updating**: the game says when a new release is out and installs it from inside the game.
  Installing a newer APK by hand over the old one also keeps everything.
- **Uninstalling the app deletes that folder, saves included**: copy `saves/` somewhere else first.

## Reporting problems

- Problems with the Android app or the Thor: [issues here](https://github.com/sonofskywalker3/khdays-358-android/issues).
- Problems that also happen in the PC port: [upstream's issues](https://github.com/Yokimitsuro/khdays-port/issues),
  where the shared code lives.

Say what happened and what you were doing when it did.

## Building

The Android app is the `android/` Gradle project around upstream's `port/`. On Windows, with Python,
`pycparser`, Visual Studio's C++ tools (for the source preparation), the Android SDK and NDK:

```powershell
py .\port\tools\build_android.py --release
```

The APK lands in `android/build/outputs/apk/release/`. Upstream's
[README](https://github.com/Yokimitsuro/khdays-port#getting-started) explains the port's own build,
its source preparation and the extracted game data.
