/*
 * media.c -- javax.microedition.media: Manager, Player, VolumeControl.
 *
 * Doom RPG's audio is a handful of tiny Standard MIDI files embedded in the JAR.
 * Rather than hand-roll a MIDI sequencer, we hand each SMF to Windows' MCI
 * (the same engine Media Player uses) by spilling the bytes to a temp .mid and
 * issuing "open"/"play". Real General MIDI playback for ~30 lines of glue.
 * On non-Windows the midi_* layer degrades to silence.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <stdio.h>

#define STATE_REALIZED 200
#define STATE_STARTED  400

/* ===== Manager ============================================================= */
jref m_javax_microedition_media_Manager__createPlayer__Ljava_io_InputStreamLjava_lang_String__Ljavax_microedition_media_Player(
        jref in, jref mime) {
    (void)mime;
    StreamObj *s = (StreamObj *)in;
    /* We back Player and its VolumeControl with one object/class: getControl()
     * returns the player itself, and its method table carries both sets. */
    PlayerObj *p = (PlayerObj *)j_new(&CLASS_javax_microedition_media_control_VolumeControl);
    if (s && s->buf) p->midi = midi_load(s->buf, s->len);
    p->loop = 1; p->state = 0;
    return (jref)p;
}

/* ===== Player ============================================================== */
void m_javax_microedition_media_Player__realize____V(jref this_) {
    ((PlayerObj *)this_)->state = STATE_REALIZED;
}
void m_javax_microedition_media_Player__setLoopCount__I__V(jref this_, jint n) {
    ((PlayerObj *)this_)->loop = n;
}
void m_javax_microedition_media_Player__start____V(jref this_) {
    PlayerObj *p = (PlayerObj *)this_;
    if (p->midi) midi_play(p->midi, p->loop < 0 || p->loop > 1);
    p->state = STATE_STARTED;
}
void m_javax_microedition_media_Player__close____V(jref this_) {
    PlayerObj *p = (PlayerObj *)this_;
    if (p->midi) { midi_stop(p->midi); p->midi = 0; }
    p->state = 0;
}
jint m_javax_microedition_media_Player__getState____I(jref this_) {
    return ((PlayerObj *)this_)->state;
}
jref m_javax_microedition_media_Controllable__getControl__Ljava_lang_String__Ljavax_microedition_media_Control(
        jref this_, jref name) {
    (void)name; return this_;   /* the player is its own VolumeControl (see above) */
}
jint m_javax_microedition_media_control_VolumeControl__setLevel__I__I(jref this_, jint level) {
    (void)this_; return level;  /* TODO: MCI volume */
}

/* ===========================================================================
 * winmm/MCI MIDI backend (midi_* declared in runtime.h)
 * =========================================================================== */
#if defined(_WIN32)
#include <windows.h>

typedef struct MidiClip { char alias[32]; char path[MAX_PATH]; } MidiClip;
static int g_clip_seq = 0;

void midi_init(void) {}

void *midi_load(const uint8_t *smf, int len) {
    MidiClip *c = (MidiClip *)j_alloc(sizeof(MidiClip));
    char tmp[MAX_PATH];
    GetTempPathA(MAX_PATH, tmp);
    snprintf(c->alias, sizeof c->alias, "drpgmidi%d", ++g_clip_seq);
    snprintf(c->path, sizeof c->path, "%s%s.mid", tmp, c->alias);
    FILE *f = fopen(c->path, "wb");
    if (f) { fwrite(smf, 1, (size_t)len, f); fclose(f); }
    return c;
}
void midi_play(void *handle, int loop) {
    MidiClip *c = (MidiClip *)handle;
    if (!c) return;
    char cmd[MAX_PATH + 64];
    snprintf(cmd, sizeof cmd, "open \"%s\" type sequencer alias %s", c->path, c->alias);
    mciSendStringA(cmd, 0, 0, 0);
    snprintf(cmd, sizeof cmd, "play %s%s from 0", c->alias, loop ? " repeat" : "");
    mciSendStringA(cmd, 0, 0, 0);
}
void midi_stop(void *handle) {
    MidiClip *c = (MidiClip *)handle;
    if (!c) return;
    char cmd[64];
    snprintf(cmd, sizeof cmd, "close %s", c->alias);
    mciSendStringA(cmd, 0, 0, 0);
}
void midi_shutdown(void) { mciSendStringA("close all", 0, 0, 0); }

#else  /* non-Windows: silent stub */
void  midi_init(void) {}
void *midi_load(const uint8_t *smf, int len) { (void)smf; (void)len; return 0; }
void  midi_play(void *h, int loop) { (void)h; (void)loop; }
void  midi_stop(void *h) { (void)h; }
void  midi_shutdown(void) {}
#endif
