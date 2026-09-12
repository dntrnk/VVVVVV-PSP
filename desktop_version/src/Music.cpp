#define MUSIC_DEFINITION
#include "Music.h"

#include "Alloc.h"
#include "BinaryBlob.h"
#include "FileSystemUtils.h"
#include "Game.h"
#include "Graphics.h"
#include "Map.h"
#include "Script.h"
#include "Unused.h"
#include "UtilityClass.h"
#include "Vlogging.h"

#include "pspaalib.h"

#define VVV_MAX_VOLUME 128
#define VVV_MAX_CHANNELS 8

static bool soundLoaded = false;

struct SoundTrack {
    int channel;          /* PSPAALIB channel */
    bool valid;
    bool loading;         /* hack to avoid double-free */
    unsigned char *mem;   /* we keep our own copy of the WAV data */
    size_t memLen;
};

static SoundTrack soundTracks[28];
static int soundVolume = VVV_MAX_VOLUME;

static const char *soundNames[] = {
    "sounds/jump.wav",
    "sounds/jump2.wav",
    "sounds/hurt.wav",
    "sounds/souleyeminijingle.wav",
    "sounds/coin.wav",
    "sounds/save.wav",
    "sounds/crumble.wav",
    "sounds/vanish.wav",
    "sounds/blip.wav",
    "sounds/preteleport.wav",
    "sounds/teleport.wav",
    "sounds/crew1.wav",
    "sounds/crew2.wav",
    "sounds/crew3.wav",
    "sounds/crew4.wav",
    "sounds/crew5.wav",
    "sounds/crew6.wav",
    "sounds/terminal.wav",
    "sounds/gamesaved.wav",
    "sounds/crashing.wav",
    "sounds/blip2.wav",
    "sounds/countdown.wav",
    "sounds/go.wav",
    "sounds/crash.wav",
    "sounds/combine.wav",
    "sounds/newrecord.wav",
    "sounds/trophy.wav",
    "sounds/rescue.wav"
};

static void loadAllSounds(void)
{
    if (soundLoaded) return;

    for (int i = 0; i < 28; i++) {
        soundTracks[i].valid = false;
        soundTracks[i].channel = -1;
        soundTracks[i].mem = NULL;
        soundTracks[i].memLen = 0;
    }

    for (int i = 0; i < 28; i++) {
        unsigned char *mem = NULL;
        size_t len = 0;
        FILESYSTEM_loadAssetToMemory(soundNames[i], &mem, &len);
        if (mem == NULL) {
            vlog_warn("Sound file not found: %s", soundNames[i]);
            continue;
        }

        /* PSPAALIB channel: WAV channels start at 17 (PSPAALIB_CHANNEL_WAV_1) */
        int ch = PSPAALIB_CHANNEL_WAV_1 + i;
        int ret = AalibLoadFromMemory(mem, (int)len, ch, TRUE);
        if (ret != PSPAALIB_SUCCESS) {
            vlog_error("Failed to load sound %s (channel %d): error %d", soundNames[i], ch, ret);
            free(mem);
            continue;
        }

        /* Keep a copy so we can free it later. LoadWavFromMemory copied the
         * audio data, but it still references fmt info etc. Actually with
         * loadToRam=TRUE it copies everything it needs. So we can free mem. */
        free(mem);

        soundTracks[i].valid = true;
        soundTracks[i].channel = ch;
        soundTracks[i].mem = NULL;
        soundTracks[i].memLen = 0;

        vlog_info("Loaded sound %s on channel %d", soundNames[i], ch);
    }

    soundLoaded = true;
}

static void unloadAllSounds(void)
{
    for (int i = 0; i < 28; i++) {
        if (soundTracks[i].valid) {
            UnloadWav(soundTracks[i].channel - PSPAALIB_CHANNEL_WAV_1);
            soundTracks[i].valid = false;
            soundTracks[i].channel = -1;
        }
    }
    soundLoaded = false;
}

static bool musicPaused = true;
static int musicVolume = VVV_MAX_VOLUME;
static int currentMusicTrack = -1;
static bool musicValid = false;

musicclass::musicclass(void)
{
    safeToProcessMusic = false;
    m_doFadeInVol = false;
    m_doFadeOutVol = false;
    musicVolume = 0;

    user_music_volume = USER_VOLUME_MAX;
    user_sound_volume = USER_VOLUME_MAX;

    currentsong = -1;
    haltedsong = -1;
    nicechange = -1;
    nicefade = false;
    quick_fade = true;

    usingmmmmmm = false;
    mmmmmm = false;
    num_pppppp_tracks = 0;
    num_mmmmmm_tracks = 0;
}

void musicclass::init(void)
{
    if (AalibInit() != PSPAALIB_SUCCESS) {
        vlog_error("Unable to initialize PSPAALIB");
        return;
    }

    loadAllSounds();

    // music init would go here (OGG from binary blob)
    musicValid = false;
    musicPaused = true;
    currentMusicTrack = -1;
    vlog_info("Music initialized (sounds only, music pending)");
}

void musicclass::destroy(void)
{
    unloadAllSounds();

    pppppp_blob.clear();
    mmmmmm_blob.clear();
    musicValid = false;
}

void musicclass::play(int t)
{
    if (mmmmmm && usingmmmmmm)
    {
        if (num_mmmmmm_tracks > 0)
        {
            t %= num_mmmmmm_tracks;
        }
    }
    else if (num_pppppp_tracks > 0)
    {
        t %= num_pppppp_tracks;
    }

    if (mmmmmm && !usingmmmmmm)
    {
        t += num_mmmmmm_tracks;
    }

    safeToProcessMusic = true;

    if (currentsong == t && !m_doFadeOutVol)
    {
        return;
    }

    currentsong = t;
    haltedsong = -1;

    if (t == -1)
    {
        return;
    }

    /* Music is not yet implemented, so just do nothing.
     * In the future, load an OGG from the binary blob and play it. */
    vlog_info("Music play(%d) - not yet implemented", t);

    m_doFadeInVol = false;
    m_doFadeOutVol = false;
    musicVolume = VVV_MAX_VOLUME;
}

void musicclass::resume(void)
{
    if (currentsong == -1)
    {
        currentsong = haltedsong;
        haltedsong = -1;
    }
    /* Music resume - pending */
}

void musicclass::resumefade(const int fadein_ms)
{
    resume();
    fadeMusicVolumeIn(fadein_ms);
}

void musicclass::fadein(void)
{
    resumefade(3000);
}

void musicclass::pause(void)
{
    /* Music pause - pending */
}

void musicclass::haltdasmusik(void)
{
    haltdasmusik(false);
}

void musicclass::haltdasmusik(const bool from_fade)
{
    pause();
    haltedsong = currentsong;
    currentsong = -1;
    m_doFadeInVol = false;
    m_doFadeOutVol = false;
    if (!from_fade)
    {
        nicefade = false;
        nicechange = -1;
    }
}

void musicclass::silencedasmusik(void)
{
    musicVolume = 0;
    m_doFadeInVol = false;
    m_doFadeOutVol = false;
}

struct FadeState
{
    int start_volume;
    int end_volume;
    int duration_ms;
    int step_ms;
};

static struct FadeState fade;

enum FadeCode
{
    Fade_continue,
    Fade_finished
};

static enum FadeCode processmusicfade(struct FadeState* state, int* volume)
{
    int range;
    int new_volume;

    if (state->duration_ms == 0
    || state->start_volume == state->end_volume
    || state->step_ms >= state->duration_ms)
    {
        *volume = state->end_volume;
        state->step_ms = 0;
        return Fade_finished;
    }

    range = state->end_volume - state->start_volume;
    new_volume = range * state->step_ms / state->duration_ms;
    new_volume += state->start_volume;

    *volume = new_volume;

    state->step_ms += game.get_timestep();

    return Fade_continue;
}

void musicclass::fadeMusicVolumeIn(int ms)
{
    if (halted())
    {
        return;
    }

    m_doFadeInVol = true;
    m_doFadeOutVol = false;

    musicVolume = 0;

    fade.step_ms = 0;
    fade.duration_ms = ms;
    fade.start_volume = 0;
    fade.end_volume = VVV_MAX_VOLUME;
}

void musicclass::fadeMusicVolumeOut(const int fadeout_ms)
{
    if (halted())
    {
        return;
    }

    m_doFadeInVol = false;
    m_doFadeOutVol = true;

    fade.step_ms = 0;
    fade.duration_ms = fadeout_ms * musicVolume / VVV_MAX_VOLUME;
    fade.start_volume = musicVolume;
    fade.end_volume = 0;
}

void musicclass::fadeout(const bool quick_fade_ /*= true*/)
{
    fadeMusicVolumeOut(quick_fade_ ? 500 : 2000);
    quick_fade = quick_fade_;
}

void musicclass::processmusicfadein(void)
{
    enum FadeCode fade_code = processmusicfade(&fade, &musicVolume);
    if (fade_code == Fade_finished)
    {
        m_doFadeInVol = false;
    }
}

void musicclass::processmusicfadeout(void)
{
    enum FadeCode fade_code = processmusicfade(&fade, &musicVolume);
    if (fade_code == Fade_finished)
    {
        musicVolume = 0;
        m_doFadeOutVol = false;
        haltdasmusik(true);
    }
}

void musicclass::processmusic(void)
{
    if (!safeToProcessMusic)
    {
        return;
    }

    if (m_doFadeInVol)
    {
        processmusicfadein();
    }

    if (m_doFadeOutVol)
    {
        processmusicfadeout();
    }

    if (nicefade && halted())
    {
        play(nicechange);
        nicechange = -1;
        nicefade = false;
    }
}

void musicclass::niceplay(int t)
{
    if ((!mmmmmm && currentsong != t)
    || (mmmmmm && usingmmmmmm && currentsong != t)
    || (mmmmmm && !usingmmmmmm && currentsong != t + num_mmmmmm_tracks))
    {
        if (currentsong != -1)
        {
            fadeout(false);
        }
        nicefade = true;
    }
    nicechange = t;
}

static const int areamap[] = {
    4, 3, 3, 3, 3, 3, 3, 3, 4,-2, 4, 4, 4,12,12,12,12,12,12,12,
    4, 3, 3, 3, 3, 3, 3, 4, 4,-2, 4, 4, 4, 4,12,12,12,12,12,12,
    4, 4, 4, 4, 3, 4, 4, 4, 4,-2, 4, 4, 4, 4,12,12,12,12,12,12,
    4, 4, 4, 4, 3, 4, 4, 4, 4,-2, 4, 4, 1, 1, 1, 1,12,12,12,12,
    4, 4, 3, 3, 3, 4, 4, 4, 4,-2,-2,-2, 1, 1, 1, 1, 4, 4, 4, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 1, 1, 1, 1, 1, 1,11,11,-1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 1, 1, 1, 1, 1, 1, 1,11,11,11,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 1, 1, 1, 1, 1, 1, 1, 1, 1,11,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 4, 4, 4, 1, 1, 1, 1, 1, 1, 3,
    4, 4, 4, 4, 4, 4, 4, 4,-2,-2, 4, 4, 4, 1, 1, 1, 1, 1, 1, 4,
    4, 4,-1,-1,-1, 4, 4, 4, 4,-2, 4, 4, 4, 1, 1, 1, 1, 1, 1, 4,
    4, 4,-1,-1,-1, 4, 4, 4, 4,-2, 4, 1, 1, 1, 1, 1, 1, 1, 1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 4, 1, 1, 1, 1, 1, 1, 4, 1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 4, 1, 1, 1, 1, 1, 1, 4, 1, 4,
    4, 4, 4, 4, 4, 4, 4, 4, 4,-2, 4,-1,-3, 4, 4, 4, 4, 4, 1, 4,
    4, 4, 4, 4, 4, 3, 3, 3, 4,-2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 4, 3, 3, 3, 3, 3, 3, 4,-2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    4, 3, 3, 3, 3, 3, 3, 3, 4,-2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 4, 4, 3, 4,-2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
    3, 3, 3, 3, 3, 4, 4, 3, 4,-2, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4
};

SDL_COMPILE_TIME_ASSERT(areamap, SDL_arraysize(areamap) == 20 * 20);

void musicclass::changemusicarea(int x, int y)
{
    int room;
    int track;

    if (script.running)
    {
        return;
    }

    room = musicroom(x, y);

    if (!INBOUNDS_ARR(room, areamap))
    {
        SDL_assert(0 && "Music map index out-of-bounds!");
        return;
    }

    track = areamap[room];

    switch (track)
    {
    case -1:
        return;
    case -2:
        if (graphics.setflipmode)
        {
            track = 9;
        }
        else
        {
            track = 2;
        }
        break;
    case -3:
        if (game.intimetrial)
        {
            track = 1;
        }
        else
        {
            track = 4;
        }
        break;
    }

    niceplay(track);
}

void musicclass::playef(int t)
{
    if (t < 0 || t >= 28) {
        return;
    }

    if (!soundLoaded) {
        loadAllSounds();
    }

    if (!soundTracks[t].valid) {
        return;
    }

    int ch = soundTracks[t].channel;

    /* If the sound is already playing, stop it first so we can restart. */
    if (AalibGetStopReason(ch) == PSPAALIB_STOP_NOT_STOPPED) {
        AalibStop(ch);
    }

    int ret = AalibPlay(ch);
    if (ret != PSPAALIB_SUCCESS) {
        vlog_warn("AalibPlay failed for sound %d (channel %d): error %d", t, ch, ret);
    }
}

void musicclass::pauseef(void)
{
    /* Pause all sound effects - not straightforward with PSPAALIB.
     * We could just ignore this, or iterate over active channels.
     * For now, do nothing. */
}

void musicclass::resumeef(void)
{
    /* Resume all sound effects - not straightforward with PSPAALIB. */
}

bool musicclass::halted(void)
{
    /* Music is halted if not playing. Since music is a stub, always true. */
    return true;
}

void musicclass::updatemutestate(void)
{
    if (game.muted)
    {
        soundVolume = 0;
        musicVolume = 0;
        for (int i = 0; i < 28; i++) {
            if (soundTracks[i].valid) {
                AalibSetVolume(soundTracks[i].channel, (AalibVolume){0.0f, 0.0f});
            }
        }
    }
    else
    {
        float vol = (float)(VVV_MAX_VOLUME * user_sound_volume / USER_VOLUME_MAX) / VVV_MAX_VOLUME;
        if (vol > 1.0f) vol = 1.0f;
        soundVolume = (int)(vol * VVV_MAX_VOLUME);
        for (int i = 0; i < 28; i++) {
            if (soundTracks[i].valid) {
                AalibSetVolume(soundTracks[i].channel, (AalibVolume){vol, vol});
            }
        }

        if (game.musicmuted)
        {
            musicVolume = 0;
        }
        else
        {
            musicVolume = VVV_MAX_VOLUME * user_music_volume / USER_VOLUME_MAX;
        }
    }
}