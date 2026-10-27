# The lower screen on a taller second display

A second display taller than 4:3 (the AYN Thor's lower panel, 1240 x 1080) shows
the lower screen in the DS's shape with black bars above and below it, or, with
Settings > Video > Second Screen Aspect Ratio set to Fullscreen (the default),
as tall as the display (`host/screens/lower_layout.c`). Nothing is stretched: a
screen the port knows is laid out again in the taller picture; one it does not
know keeps the DS's shape and its bars.

The lower picture arrives at the display's width (`host/android/bottom_screen.c`).
Its DS pixels, read at their middles, tell the screen (`lower_screens.c`, each by
a few pixels that hold while the game dims it too). For the screen and what was
measured on it (a divider's row, a grid's width) a map is made once: for each
display pixel, the picture's pixel it shows. Each frame is then a copy through
the map, and a touch goes back through it to the DS pixel it shows.

Rows and columns below are the DS's; the taller picture has `extra` rows more
(31 on the Thor).

| Screen | Told by | Laid out |
|---|---|---|
| The field's INFORMATION panel (Mission Mode, the story; the pause, dimmed) | row 23 one grey across, the panel's white at (30, 100), the header's black | the header and objective (rows 0-27) at the top, the mission's or day's bar (148-191) at the bottom; the panel grows: the target's box and the map's box taller, their pictures in their middles, the map box's inside continued from its edge rows; the emblem pattern beside it again every 48 rows |
| The save list (Load, its guest and slot dialogs) | row 14 one blue across, row 15 black | the header (0-15) at the top, everything from the row above the full-width grey divider at the bottom, the list between enlarged 1.17x both ways about column 130.5 |
| Mission Mode's character select | row 176 white across, no tab icons | the title (0-23) at the top, the help from row 175 at the bottom, the grid and PRESS START enlarged both ways by what leaves 4 columns free beside the grid (1.2x at most: 1.2x for the first roster, about 1.03x for every character), the striped backdrop above and below |
| The camp's status panel | the red panel at (30, 96) and (60, 100), the stats' grey at (150, 100) | not enlarged (both panels span the width): each panel's entries spaced out, a plain row repeated under each divider |
| A list of missions (Mission Mode's, the story's day by day) | the frame's edge (columns 9 and 10) and the scroll track's rounded end at (240, 173), no header bar across row 14 (the tutorials share the frame) | the list grows to the bottom: the rows under its frame (176 on) composed again without the frame's layer (BG1) and the sprites (`gfx_render.h` khdays_gfx_render_set_lower_rows), so the next entry the game drew under the frame shows; the track's rounded end at its new bottom; widened edge to edge (the left margin left out, column 191 between the names and the crowns repeated); the L and R icons at their own place; the down arrow in the widened list's middle |
| A mission's summary (and its Embark dialog) | the green L and R icons, row 176 a line across, the page's grey or white at (100, 120) | the card, tabs and page enlarged 256/240 so they fill the width, the page one ruled line longer (its scroll track longer above its end); the scanline band, the white line and Embark / Back at their own size at the bottom; the L and R icons at their own size in the corners |

Rows the layout wants apart from the DS's (a list's next entry under its frame)
are asked of the renderer for the next frames (`gfx_render.c`: the lower engine's
rows composed again from a DS row on, some layers left out, `compose.h`
`KD_LAYERS_OFF`), sent under the lower picture (`gpu_present.c`) and read from
there; the frame before they arrive keeps the DS's shape.

Not laid out yet (the DS's shape): the Panels page, Roxas's Diary and the
tutorials' list (each shows every entry it draws), the title, the shops, the
results, the event and cutscene screens.
