/* The game's update functions the port's decision watches (situation_decide.c): for each frame
 * the inputs say which of them ran (situation.h, KhdaysSituationInputs.updates). One line per
 * function, by the decomp's name; each one's meaning is in docs/RUNTIME_MAP.md section 15.6.
 * Included with KHDAYS_UPDATE(name) defined. */
KHDAYS_UPDATE(Ov000_TickMenuLoop)       /* the title's menu waiting for a choice (RUNTIME_MAP 15.1) */
KHDAYS_UPDATE(Ov000_TickLoadScene)      /* the title's save list being made (play walk f1147) */
KHDAYS_UPDATE(Ov000_TickSelectionScene) /* the title's save list, a file to choose (play walk f1161-1945) */
KHDAYS_UPDATE(Ov008_OpenShopState)      /* Mission Mode's camp opens its shop (camp_shop script f3097) */
KHDAYS_UPDATE(Ov008_WaitShopClosed)     /* and waits while the shop is open (f3099-4525) */
KHDAYS_UPDATE(Ov023_SceneRun)           /* the field's event runs (its status is the scene's only then) */
