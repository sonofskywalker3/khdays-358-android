/* The field's HUD. */
#ifndef KHDAYS_UI_HUD_H
#define KHDAYS_UI_HUD_H

#include <stdint.h>

/* KHDAYS_TRACE_HUD's frames, read at start. */
void khdays_ui_hud_init(void);

/* Called at each VBlank. At the frames KHDAYS_TRACE_HUD lists, it reports
 * each text layer's groups of cells that show something, on stderr and in
 * the session log. */
void khdays_ui_hud_vblank(uint32_t frame);

/* Whether the field shows a dialog this frame (situation.h): its box, the speaker's face and name
 * plate take the upper screen's bottom where the party's portrait is, with characters of the same
 * file, so the upper screen's pieces are not taken then. */
void khdays_ui_hud_set_dialog(int dialog);

#endif
