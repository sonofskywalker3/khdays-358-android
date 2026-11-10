# A Nintendo 3DS version: what it would take

Nobody is working on one. This is what was found when the question was looked
into (2026-11-09, against the port as it stood then), kept for whoever comes
back to it. The drawing is the part that decides most of it, so read
`RENDERING.md` again first: what is said here about it dates quickly.

## What it would be for

The 3DS already plays the DS game, so a port only earns its place by what the
DS mode cannot do:

- the 3D drawn at the 3DS's own resolution, wide on the upper screen;
- movement and the camera on the analog controls;
- perhaps stereoscopic 3D.

Everything else would be the game as the port runs it everywhere.

## What carries over

The game's C, the runtime under it (`port/runtime`), the HAL (`port/hal`) and
the 3D geometry (`port/gfx/g3d.h`), which answers the game in the DS's fixed
point and leaves each frame's 3D as a list of polygons. None of it depends on
the system it runs on.

## What does not

**The drawing.** The PC and Android share one renderer (`port/host/render`, on
SDL3's GPU API): the 2D is composed per pixel by a shader
(`shaders/compose2d.hlsl`) and the 3D rasterized by shaders too
(`raster3d.hlsl`, `native3d*.hlsl`). The 3DS's graphics chip (PICA200) has
programmable vertex shaders but fixed pixel stages and no compute, so none of
that runs there. A 3DS version needs a renderer of its own. What there is to
start from:

- the software engines the card's picture is checked against (`port/gpu`:
  `gpu2d.c`, `gpu3d.c`), which draw at the DS's resolution;
- the polygon lists themselves, which a 3DS drawer could give to the chip's
  own rasterizer.

The likely shape is the 2D in software and the 3D on the chip. What the DS's
3D does that the chip's fixed stages may not (the toon table, edge marking,
fog, the translucent polygons' ID rule, 6-bit colour) would each have to be
matched, approximated or left out; this was not gone into.

**The memory.** The game's memory is a window the port reserves whole
(`runtime/core/window.h`): the heaps' region alone runs from 0x20000000 to
0x80000000, committed as the game touches it. That takes a 64-bit address
space. The 3DS is 32-bit, with about 64 MB for a program on the original
models and 124 MB on the New 3DS. The game itself needs little (the DS had
4 MB), so it fits, but the window has to be laid out again.

**The threads.** The switch between the game's threads is written per CPU
(`host/app/context_x64.asm`, `context_arm64.S`); a 32-bit ARM one is needed.

**The rest of the host.** The settings menu (Dear ImGui over the same GPU
API), the HD videos and the sound output are the PC's and Android's; each
needs its 3DS counterpart or is left out.

## Speed

Not measured. The DS did its geometry, its 2D and its sound mixing in
hardware; in a 3DS version the geometry, the 2D and the mixing would all be
the CPU's work, beside the game. The original 3DS's CPU is 268 MHz. The guess
is that only the New 3DS would manage, and it is a guess.

## What the picture would gain

Both 3DS screens are 240 lines high against the DS's 192: 1.25 times. The
port's widescreen (`RENDERING.md`, "Widescreen") at 320 DS columns comes to
exactly the upper screen's 400 pixels at that scale, and the lower screen's
320 x 240 is the DS's 256 x 192 at the same. So the 3D can be drawn sharp at
400 x 240, a quarter more lines than the DS and wider. The 2D at 1.25 times
is either soft or uneven, or stays at the DS's size with a border.

Stereoscopic 3D means drawing the 3D twice a frame, once for each eye, so it
depends on the speed left over.

## The controls

The original 3DS has one circle pad: it can move Roxas or turn the camera, not
both. The camera on a stick of its own means the New 3DS's C-stick (or the
Circle Pad Pro).

## Where it stands

The systems planned upstream are Linux, then macOS, Windows on ARM and the
Nintendo Switch (`README.md`); the 3DS is not among them. A 3DS version is
possible, and a much larger piece of work than the Android one was, most of it
the renderer.
