# Adreno 619 Vulkan driver: a large compute shader does not receive uniform block members past byte 128

## Summary

On an Adreno 619 with Qualcomm's Vulkan driver 512.762.40, one large compute shader does not get the
members of its uniform block that lie at byte offset 128 or later. The block is 144 bytes
(`UNIFORM_BUFFER_DYNAMIC`, set 2, binding 0). Changing those members has no effect on the shader's
output. After a dispatch of a second, unrelated compute pipeline, the same members hold leftover values
instead.

Cutting the block to 128 bytes makes every member arrive correctly. A small shader with the identical
144-byte block reads all of it correctly, so the fault depends on the shader, not on the block alone.

## Device

| | |
| --- | --- |
| Device | Samsung Galaxy Tab A9+ (SM-X210) |
| GPU | Adreno (TM) 619 |
| Driver | Qualcomm Technologies Inc. Adreno Vulkan Driver 512.762.40 (driverVersion 0x802fa028) |
| Vulkan API | 1.3.128 |

The same application draws correctly on an Adreno 740 (AYN Thor), on Mali-G57 and Mali-G76 devices, and
on desktop Vulkan and D3D12.

## How it showed

The application draws a 2D picture with two compute passes: a small one (`layers2d`), then a large one
(`compose2d`). In `compose2d`'s block, the last four members were `row0`, `layers_off`, `layers1x_rows`
and `pre_mode`, at offsets 128 to 140.

- `compose2d` dispatched alone: those four behave as if they were 0, whatever the application writes.
- `compose2d` dispatched after `layers2d`: `row0` behaves as if it were 131. The picture comes out
  shifted by 131 rows, wrapping round.
- A memory barrier between the two dispatches, a copy between them, submitting them in separate command
  buffers and waiting, or creating the pipelines in the other order: no change.
- A dispatch of a third, small pipeline between the two: the four members go back to behaving as 0.

## Reproducing it

The files here need no application data: every input buffer is zero.

| File | What it is |
| --- | --- |
| `vkrun.c` | A command-line Vulkan program. It lays its pipelines out the way SDL3's GPU API does: set 0 storage buffers, set 1 storage images, set 2 one dynamic uniform buffer. |
| `compose2d_144.spv` | The shader with the 144-byte block. |
| `compose2d_128.spv` | The same shader with the block packed into 128 bytes. |
| `layers2d.spv` | The small shader dispatched first in the application. |
| `probe.hlsl`, `probe.spv` | A small shader with the identical 144-byte block. It writes each of the 36 words to an image. |
| `make_zero.py` | Writes `zero.bin`, the all-zero input file. |

Build and push (Android NDK, any recent version):

```sh
clang --target=aarch64-linux-android29 -O1 -o vkrun vkrun.c -lvulkan
python make_zero.py
adb shell mkdir -p /data/local/tmp/vkrun
adb push vkrun zero.bin compose2d_144.spv compose2d_128.spv layers2d.spv /data/local/tmp/vkrun/
adb shell chmod 755 /data/local/tmp/vkrun/vkrun
```

Run from `/data/local/tmp/vkrun`. The program clears the target image to magenta, dispatches the shader,
and prints how many texels are still magenta.

The shader's `pre_mode` member decides whether it writes the target image at all. With `pre_mode` 1 it
writes a different image and leaves the target alone.

```sh
# 144-byte block, pre_mode = 1 (word 35). Expected: all 196608 texels unwritten.
./vkrun zero.bin compose2d_144.spv out.rgba layers=layers2d.spv seq=1 lgx=0 word=35:1

# 128-byte block, the same request.
REMAP=3 ./vkrun zero.bin compose2d_128.spv out.rgba layers=layers2d.spv seq=1 lgx=0 word=35:1
```

Results on the Tab A9+:

| Shader | `pre_mode` written | Unwritten texels | |
| --- | --- | --- | --- |
| `compose2d_144.spv` | 0 | 0 of 196608 | correct |
| `compose2d_144.spv` | 1 | 0 of 196608 | **wrong**: `pre_mode` was not seen |
| `compose2d_128.spv` | 0 | 0 of 196608 | correct |
| `compose2d_128.spv` | 1 | 196608 of 196608 | correct |

## What the driver reports about the shader

`VK_KHR_pipeline_executable_properties` is supported on this driver and returns statistics. It returns no
internal representations, so the compiled code could not be read.

For `compose2d_144.spv` the statistics include a "preamble" of 2843 instructions with 23 texture reads.
`layers2d.spv` has a preamble of 94 instructions with 4 texture reads. The leftover values seen after a
`layers2d` dispatch suggest the two shaders' preamble results share storage, but that is a guess.

## Workaround

Keep the uniform block of a large compute shader at 128 bytes or less. In this application, four small
members were packed into one word.
