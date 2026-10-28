# The lower screen on a taller second display

A second display taller than 4:3 (the AYN Thor's lower panel, 1240 x 1080) shows
the lower screen in the DS's shape with black bars above and below it, or, with
Settings > Video > Second Screen Aspect Ratio set to Fullscreen (the default),
as tall as the display (`host/screens/lower_layout.c`). Nothing is stretched: a
screen the port knows is laid out again in the taller picture; one it does not
know keeps the DS's shape and its bars.

The lower picture arrives at the display's width (`host/android/bottom_screen.c`).
Its DS pixels, read at their middles, tell the screen (`lower_screens.c` and
`lower_pages.c`, each by a few pixels that hold while the game dims it too).
For the screen and what was measured on it (a divider's row, a grid's width) a
map is made once: for each display pixel, the picture's pixel it shows. Each
frame is then a copy through the map, and a touch goes back through it to the
DS pixel it shows.

Rows and columns below are the DS's; the taller picture has `extra` rows more
(31 on the Thor).

| Screen | Told by | Laid out |
|---|---|---|
| The field's INFORMATION panel (Mission Mode, the story; the pause, dimmed) | row 23 one grey across, the panel's white at (30, 100), the header's black; or, while the header slides in as a mission starts, the panel's lilac top edge across row 30 and its white | the header and objective (rows 0-27) at the top, the mission's or day's bar (148-191) at the bottom; the panel grows: the target's box and the map's box taller, their pictures in their middles, the map box's inside continued from its edge rows; the emblem pattern beside it again every 48 rows |
| The save list (Load, its guest and slot dialogs) | row 14 one blue across, row 15 black | the header (0-15) at the top, everything from the row above the full-width grey divider at the bottom, the list between enlarged 1.17x both ways about column 130.5 |
| Mission Mode's character select | row 176 white across, no tab icons, the black band under the title (row 20) | the title (0-23) at the top, the help from row 175 at the bottom, the grid and PRESS START enlarged both ways by what leaves 4 columns free beside the grid (1.2x at most: 1.2x for the first roster, about 1.03x for every character), the striped backdrop above and below |
| The camp's status panel | the red panel at (30, 96) and (60, 100), the stats' grey at (150, 100) | not enlarged (both panels span the width): each panel's entries spaced out, a plain row repeated under each divider |
| A list of missions (Mission Mode's, the story's day by day) | the frame's edge (columns 9 and 10, at row 60, 100 or 140: the pointing hand covers it beside the chosen entry) and the scroll track's rounded end at (240, 173), their greys within 4 of 108 and 149 (the Thor draws the lists' 107 and 148, the tutorials' 109 and 150), no header bar across row 14 (the tutorials share the frame) | the list grows to the bottom: the rows under its frame (176 on) composed again without the frame's layer (BG1) and the sprites (`gfx_render.h` khdays_gfx_render_set_lower_rows), so the next entry the game drew under the frame shows; the frame's sides and the track below the DS's rows from a plain row of theirs (the one most rows share: not beside the hand or the chosen entry, not through the track's thumb); the track's rounded end at its new bottom; widened edge to edge (the left margin left out, column 191 between the names and the crowns repeated); the L and R icons at their own place; the down arrow in the widened list's middle |
| The tutorials' list | row 14 one red across under the header, row 15 black, the frame's stripes (row 16) and its grey top edge (row 26) alike at columns 30 and 200 | the game draws only the entries it shows, so they are spaced apart: the rows between entries (48, 64, ... 160) repeated alike; the scroll track (from column 224) longer at a plain row of its own |
| Roxas's Diary's list | the backdrop's stripes (rows 0 and 2, again at 188) and its plain green (row 12, either side of the scroll arrows) each one colour across | the same: the rows between entries (32, 48, ... 160) repeated alike, the track (from column 232) longer at a plain row |
| The Panels page | the help box's white lines (rows 161 and 190) and the dark line over it, the board's grey frame at (4, 60), (4, 100), (4, 140), the stock's blue under its list | the board (columns 0-94) at its own size, its frame grown above and below its grid (rows 22 and 152); the stock's entries apart (each one's plain first row, 24 to 136, repeated); its track (from column 232) longer at a plain row; the help box at the bottom |
| One colour across (the fade to white at a game over) | every DS pixel within 2 of the first, not black | all of the display that colour, no bars |
| A mission's summary (and its Embark dialog) | the card's red edges at (12, 24) and (240, 24), row 176 a line across, the page's grey or white at (100, 120), each also dimmed under the dialog (the story's summary has no L and R icons) | the card, tabs and page enlarged 256/240 so they fill the width, the page one ruled line longer (its scroll track longer above its end); the scanline band, the white line and Embark / Back at their own size at the bottom; Mission Mode's L and R icons at their own size in the corners |

Rows the layout wants apart from the DS's (a list's next entry under its frame)
are asked of the renderer for the next frames (`gfx_render.c`: the lower engine's
rows composed again from a DS row on, some layers left out, `compose.h`
`KD_LAYERS_OFF`), sent under the lower picture (`gpu_present.c`) and read from
there; the frame before they arrive keeps the DS's shape.

Not laid out yet (the DS's shape): the title, the shops and synthesis, the
tutorial pages shown during a mission, the results, the event and cutscene
screens.
