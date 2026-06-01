/*
 * devsaves.h -- backup/restore of the game's own RecordStore saves.
 *
 * The game persists progress to Config/Player/Player2/World .rms files (the
 * same stores the reverse-engineered DoomRPG-RE uses). These copy that set to
 * and from numbered slots so the player can keep multiple real save games.
 * Unlike the emulator save states, these are the game's native saves and load
 * through the game's normal Continue flow.
 */
#ifndef DOOMRPG_DEVSAVES_H
#define DOOMRPG_DEVSAVES_H

#ifdef __cplusplus
extern "C" {
#endif

int devsaves_backup(int slot);    /* copy live .rms -> slot. returns files copied, <0 error */
int devsaves_restore(int slot);   /* copy slot -> live .rms. returns files copied, <0 error */
int devsaves_slot_exists(int slot);
int devsaves_live_exists(void);   /* is there a current game save to back up? */

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVSAVES_H */
