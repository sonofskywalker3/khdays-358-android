/* The notice of an autosave: a port addition (runtime/menus/autosave.h). */
#ifndef KHDAYS_UI_AUTOSAVE_NOTICE_H
#define KHDAYS_UI_AUTOSAVE_NOTICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Called by the HUD's emit at every VBlank: for three seconds after an autosave is written or fails,
 * the element "hud.autosave": its words (the file's number in them) at the upper screen's top left, in
 * the game's 8-dot font, white with a black edge. */
void khdays_ui_autosave_notice_emit(void);

#ifdef __cplusplus
}
#endif

#endif
