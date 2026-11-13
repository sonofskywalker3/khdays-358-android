// compose2d.hlsl built for the 2D composed once a DS dot (gfx/spread2d.h, gfx_render.c `sparse`): its DS-sized
// pass's verdicts for spread2d.hlsl, and the composition of the dots spread2d.hlsl lists. Apart from compose2d.hlsl
// as the screens are composed without it, whose code is kept as it was: on an Adreno 619 (Tab A9+) the shader with
// all of this in it, unused, made the field 2 pictures a second slower (32 for 34, 2026-11-12). Its cs_main() is
// compose2d.hlsl's.
#define COMPOSE2D_SPARSE
#include "compose2d.hlsl"
