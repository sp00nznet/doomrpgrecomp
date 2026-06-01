/*
 * midlet.c -- javax.microedition.midlet.MIDlet. getAppProperty() serves the
 * JAD/manifest entries Doom RPG reads at startup (map to boot, frame count,
 * blit tuning flags, ...). notifyDestroyed() asks the host loop to quit.
 */
#include "j2me/runtime.h"
#include "doomrpg.h"
#include <string.h>

int g_quit_requested = 0;   /* read by main loop */

/* Manifest attributes from DoomRPG.jar's MANIFEST.MF (the MIDlet reads these). */
static const struct { const char *k, *v; } MANIFEST[] = {
    { "MIDlet-Name", "Doom RPG" },
    { "MIDlet-Version", "1.8.94" },
    { "MIDlet-Vendor", "Electronic Arts" },
    { "MicroEdition-Profile", "MIDP-2.0" },
    { "MicroEdition-Configuration", "CLDC-1.0" },
    { "DoomRPG-Map", "/intro.bsp" },
    { "DoomRPG-Frames", "2" },
    { "DoomRPG-S-MaxRealized", "1" },
    { "DoomRPG-S-MaxPrefetched", "2" },
    { "DoomRPG-SkipShakeX", "1" },
    { "DoomRPG-SlowBlit", "0" },
    { "DoomRPG-SplitBlit", "0" },
    { "DoomRPG-S-Explosions", "1" },
    { "DoomRPG-SlowDrawRegion", "0" },
    { "DoomRPG-S-Limited", "0" },
    { "iDEN-MIDlet-miniJIT", "on" },
    { "Content-Folder", "Games" },
    { 0, 0 }
};

void m_javax_microedition_midlet_MIDlet___init_____V(jref this_) { (void)this_; }

jref m_javax_microedition_midlet_MIDlet__getAppProperty__Ljava_lang_String__Ljava_lang_String(jref this_, jref key) {
    (void)this_;
    char *k = j_string_to_cstr(key);
    jref result = 0;
    for (int i = 0; MANIFEST[i].k; i++) {
        if (strcmp(MANIFEST[i].k, k) == 0) { result = j_strlit(MANIFEST[i].v); break; }
    }
    free(k);
    return result;
}

void m_javax_microedition_midlet_MIDlet__notifyDestroyed____V(jref this_) {
    (void)this_; g_quit_requested = 1;
}
