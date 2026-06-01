/*
 * devaudio.h -- volume controls for the dev menu's Audio menu.
 *
 * The game's audio is all MIDI played through Windows MCI. "Master"/"Mute" act
 * on this process's Windows audio-session volume (affects everything); "Music"
 * is a best-effort MCI volume on the playing sequencer. Values are 0..100.
 */
#ifndef DOOMRPG_DEVAUDIO_H
#define DOOMRPG_DEVAUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

void devaudio_init(void);

int  devaudio_get_master(void);
void devaudio_set_master(int pct);
int  devaudio_get_music(void);
void devaudio_set_music(int pct);
int  devaudio_get_mute(void);
void devaudio_set_mute(int on);

#ifdef __cplusplus
}
#endif

#endif /* DOOMRPG_DEVAUDIO_H */
