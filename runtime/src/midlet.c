/*
 * midlet.c -- javax.microedition.midlet.MIDlet. getAppProperty() serves the
 * JAD/manifest entries Doom RPG reads at startup (map to boot, frame count,
 * blit tuning flags, ...). notifyDestroyed() asks the host loop to quit.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <string.h>

int g_quit_requested = 0;   /* read by main loop */

void m_javax_microedition_midlet_MIDlet___init_____V(jref this_) { (void)this_; }

/* Serve the game's real MANIFEST.MF attributes (g_manifest, emitted per game in
 * game_entry.c) -- so each title gets its own map/frame/blit-tuning properties. */
jref m_javax_microedition_midlet_MIDlet__getAppProperty__Ljava_lang_String__Ljava_lang_String(jref this_, jref key) {
    (void)this_;
    char *k = j_string_to_cstr(key);
    jref result = 0;
    for (int i = 0; g_manifest[i]; i += 2) {
        if (strcmp(g_manifest[i], k) == 0) { result = j_strlit(g_manifest[i + 1]); break; }
    }
    free(k);
    return result;
}

void m_javax_microedition_midlet_MIDlet__notifyDestroyed____V(jref this_) {
    (void)this_; g_quit_requested = 1;
}

/* No external browser/dialer on the desktop; report "not handled". */
jint m_javax_microedition_midlet_MIDlet__platformRequest__Ljava_lang_String__Z(jref this_, jref url) {
    (void)this_; (void)url; return 0;
}
