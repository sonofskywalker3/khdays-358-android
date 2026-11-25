"""Generate the port's icons from a vector crown mark.

Writes the Android launcher set and the Windows icons for the game, its
installer and the mod studio. The crown is drawn at high resolution and
downsampled, so each size is clean. See ART-SOURCES.md.
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path

from PIL import Image, ImageDraw

logger = logging.getLogger(__name__)

REPO_ROOT = Path(__file__).resolve().parents[2]

# Launcher densities: directory suffix -> (legacy px, adaptive foreground px).
# Legacy icons are 48dp, adaptive foregrounds are 108dp.
DENSITIES: tuple[tuple[str, int, int], ...] = (
    ("mdpi", 48, 108),
    ("hdpi", 72, 162),
    ("xhdpi", 96, 216),
    ("xxhdpi", 144, 324),
    ("xxxhdpi", 192, 432),
)

BACKGROUND = (20, 26, 46, 255)
CROWN_FILL = (232, 230, 225, 255)
CROWN_EDGE = (120, 126, 148, 255)

# Supersampling factor for the crown path before downsampling.
SUPERSAMPLE = 8

# Crown silhouette in normalised coordinates (origin top-left, y grows down).
CROWN_PATH: tuple[tuple[float, float], ...] = (
    (0.18, 0.78),
    (0.02, 0.30),
    (0.28, 0.52),
    (0.50, 0.12),
    (0.72, 0.52),
    (0.98, 0.30),
    (0.82, 0.78),
    (0.66, 0.88),
    (0.50, 0.91),
    (0.34, 0.88),
)

# Fraction of the adaptive 108dp canvas the mark occupies. The launcher may mask
# anything outside the central 72dp, so the crown stays well inside it.
ADAPTIVE_SCALE = 0.52
# Legacy icons have no mask, so the mark can sit larger on the tile.
LEGACY_SCALE = 0.72
# Corner radius of the legacy tile, as a fraction of its size.
LEGACY_CORNER = 0.18


def draw_crown(size: int, scale: float) -> Image.Image:
    """Render the crown mark centred on a transparent square of `size` px."""
    hi = size * SUPERSAMPLE
    layer = Image.new("RGBA", (hi, hi), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)

    span = hi * scale
    offset_x = (hi - span) / 2
    offset_y = (hi - span) / 2
    points = [(offset_x + x * span, offset_y + y * span) for x, y in CROWN_PATH]

    draw.polygon(points, fill=CROWN_FILL, outline=CROWN_EDGE, width=max(1, hi // 160))
    return layer.resize((size, size), Image.LANCZOS)


def rounded_tile(size: int, radius_fraction: float) -> Image.Image:
    """A filled background tile with rounded corners."""
    hi = size * SUPERSAMPLE
    mask = Image.new("L", (hi, hi), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, hi - 1, hi - 1), radius=int(hi * radius_fraction), fill=255)
    tile = Image.new("RGBA", (hi, hi), BACKGROUND)
    tile.putalpha(mask.resize((hi, hi), Image.LANCZOS))
    return tile.resize((size, size), Image.LANCZOS)


def circular_tile(size: int) -> Image.Image:
    """A filled circular background tile."""
    hi = size * SUPERSAMPLE
    mask = Image.new("L", (hi, hi), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, hi - 1, hi - 1), fill=255)
    tile = Image.new("RGBA", (hi, hi), BACKGROUND)
    tile.putalpha(mask)
    return tile.resize((size, size), Image.LANCZOS)


def compose(background: Image.Image, mark: Image.Image) -> Image.Image:
    out = background.copy()
    out.alpha_composite(mark)
    return out


def write_icons(res_dir: Path) -> list[Path]:
    """Write every launcher asset under `res_dir`; returns the paths written."""
    written: list[Path] = []
    for suffix, legacy_px, adaptive_px in DENSITIES:
        target = res_dir / f"mipmap-{suffix}"
        target.mkdir(parents=True, exist_ok=True)

        legacy = compose(
            rounded_tile(legacy_px, LEGACY_CORNER),
            draw_crown(legacy_px, LEGACY_SCALE),
        )
        round_icon = compose(circular_tile(legacy_px), draw_crown(legacy_px, LEGACY_SCALE))
        # The adaptive foreground is transparent; the background is a colour
        # resource, so the launcher can parallax the two layers apart.
        foreground = draw_crown(adaptive_px, ADAPTIVE_SCALE)

        for name, image in (
            ("ic_launcher.png", legacy),
            ("ic_launcher_round.png", round_icon),
            ("ic_launcher_foreground.png", foreground),
        ):
            path = target / name
            image.save(path, "PNG", optimize=True)
            written.append(path)
            logger.info("wrote %s (%dx%d)", path, image.width, image.height)

    return written


# Windows .ico sizes, largest first; every size is drawn on its own.
ICO_SIZES: tuple[int, ...] = (256, 128, 64, 48, 32, 16)
# Windows icons that get the crown, relative to the repository's root.
ICO_FILES: tuple[str, ...] = (
    "port/resources/game.ico",
    "studio/resources/studio.ico",
)


def tile_icon(size: int) -> Image.Image:
    """The crown on the rounded tile, as one square image of `size` px."""
    return compose(rounded_tile(size, LEGACY_CORNER), draw_crown(size, LEGACY_SCALE))


def write_ico(path: Path) -> Path:
    """Write a Windows .ico holding every size in ICO_SIZES."""
    path.parent.mkdir(parents=True, exist_ok=True)
    images = [tile_icon(size) for size in ICO_SIZES]
    images[0].save(
        path,
        "ICO",
        sizes=[(size, size) for size in ICO_SIZES],
        append_images=images[1:],
    )
    logger.info("wrote %s (%s)", path, ", ".join(str(size) for size in ICO_SIZES))
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--res-dir",
        type=Path,
        default=REPO_ROOT / "android/src/main/res",
        help="Android res/ directory to write the mipmap folders into",
    )
    args = parser.parse_args()

    logging.basicConfig(level=logging.INFO, format="%(message)s")
    written = write_icons(args.res_dir)
    written += [write_ico(REPO_ROOT / name) for name in ICO_FILES]
    logger.info("%d files written", len(written))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
