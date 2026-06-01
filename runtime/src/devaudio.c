/*
 * devaudio.c -- master/mute via the Windows audio-session volume (Core Audio),
 * music via MCI. All game audio is MIDI through MCI, so the session volume is
 * the reliable overall control; the music slider additionally rides the MCI
 * sequencer volume where the driver supports it. 0..100 scale.
 */
#include "devaudio.h"

static int g_master = 100;
static int g_music  = 100;
static int g_mute   = 0;

#if defined(_WIN32)
#define COBJMACROS
#include <initguid.h>        /* FIRST: makes DEFINE_GUID allocate the Core Audio
                             * GUIDs in this TU (must precede the headers) */
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

/* The SDK headers only declare these GUIDs extern; allocate them here. */
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xBCDE0395,0xE52F,0x467C,0x8E,0x3D,0xC4,0x57,0x92,0x91,0x69,0x2E);
DEFINE_GUID(IID_IMMDeviceEnumerator,  0xA95664D2,0x9614,0x4F35,0xA7,0x46,0xDE,0x8D,0xB6,0x36,0x17,0xE6);
DEFINE_GUID(IID_IAudioSessionManager, 0xBFA971F1,0x4D5E,0x40BB,0x93,0x5E,0x96,0x70,0x39,0xBF,0xBE,0xE4);

/* media.c: re-issues MCI "setaudio <alias> volume to N" (0..1000) on the clip */
void midi_set_volume(int milli);

static ISimpleAudioVolume *g_sav;
static int g_com_ready;

static void audio_acquire(void) {
    if (g_sav) return;
    if (!g_com_ready) { CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); g_com_ready = 1; }
    IMMDeviceEnumerator *en = NULL;
    if (FAILED(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                                &IID_IMMDeviceEnumerator, (void **)&en)) || !en) return;
    IMMDevice *dev = NULL;
    if (SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eConsole, &dev)) && dev) {
        IAudioSessionManager *mgr = NULL;
        if (SUCCEEDED(IMMDevice_Activate(dev, &IID_IAudioSessionManager, CLSCTX_ALL, NULL,
                                         (void **)&mgr)) && mgr) {
            IAudioSessionManager_GetSimpleAudioVolume(mgr, NULL, FALSE, &g_sav);
            IAudioSessionManager_Release(mgr);
        }
        IMMDevice_Release(dev);
    }
    IMMDeviceEnumerator_Release(en);
}

static void apply_session(void) {
    audio_acquire();
    if (!g_sav) return;
    ISimpleAudioVolume_SetMasterVolume(g_sav, g_master / 100.0f, NULL);
    ISimpleAudioVolume_SetMute(g_sav, g_mute ? TRUE : FALSE, NULL);
}
static void apply_music(void) { midi_set_volume(g_mute ? 0 : g_music * 10); }

#else  /* non-Windows: no-op */
static void apply_session(void) {}
static void apply_music(void) {}
#endif

void devaudio_init(void) { apply_session(); apply_music(); }

int  devaudio_get_master(void) { return g_master; }
int  devaudio_get_music(void)  { return g_music; }
int  devaudio_get_mute(void)   { return g_mute; }

void devaudio_set_master(int pct) {
    g_master = pct < 0 ? 0 : pct > 100 ? 100 : pct; apply_session();
}
void devaudio_set_music(int pct) {
    g_music = pct < 0 ? 0 : pct > 100 ? 100 : pct; apply_music();
}
void devaudio_set_mute(int on) {
    g_mute = on ? 1 : 0; apply_session(); apply_music();
}
