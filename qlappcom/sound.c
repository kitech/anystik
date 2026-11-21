#include "sound.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#  include <windows.h>
#  include <mmsystem.h>
#else
#  include <signal.h>
#  include <spawn.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char **environ;
#  define SND_RATE     44100
#  define SND_CHANNELS 1
#  define SND_BITS     16
static pid_t g_snd_pid = -1;

static void snd_posix_stop(void) {
    if (g_snd_pid <= 0) {
        return;
    }
    if (waitpid(g_snd_pid, NULL, WNOHANG) == 0) {
        kill(g_snd_pid, SIGTERM);
        if (waitpid(g_snd_pid, NULL, WNOHANG) == 0) {
            kill(g_snd_pid, SIGKILL);
            waitpid(g_snd_pid, NULL, 0);
        }
    }
    g_snd_pid = -1;
}

static pid_t snd_spawn(const char* prog, char* const argv[]) {
    pid_t pid = -1;
    if (posix_spawnp(&pid, prog, NULL, NULL, argv, environ) != 0) {
        return -1;
    }
    return pid;
}
#endif

#if defined(_WIN32)
static HWAVEOUT g_wo;
static WAVEHDR  g_wh;
static char*    g_pcm;

static void snd_win_stop(void) {
    if (g_wo) {
        waveOutReset(g_wo);
        if (g_wh.lpData) {
            waveOutUnprepareHeader(g_wo, &g_wh, sizeof(g_wh));
        }
        waveOutClose(g_wo);
        g_wo = NULL;
    }
    if (g_pcm) {
        free(g_pcm);
        g_pcm = NULL;
    }
    memset(&g_wh, 0, sizeof(g_wh));
}

static int snd_win_play_pcm(const char* filePath) {
    FILE* f;
    long sz;
    char* data;
    WAVEFORMATEX wf;

    snd_win_stop();
    f = fopen(filePath, "rb");
    if (!f) {
        return -1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    if (sz <= 0) {
        fclose(f);
        return -1;
    }
    data = (char*)malloc((size_t)sz);
    if (!data) {
        fclose(f);
        return -1;
    }
    if (fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    memset(&wf, 0, sizeof(wf));
    wf.wFormatTag      = WAVE_FORMAT_PCM;
    wf.nChannels       = SND_CHANNELS;
    wf.nSamplesPerSec  = SND_RATE;
    wf.wBitsPerSample  = SND_BITS;
    wf.nBlockAlign     = (WORD)(SND_CHANNELS * SND_BITS / 8);
    wf.nAvgBytesPerSec = SND_RATE * wf.nBlockAlign;

    if (waveOutOpen(&g_wo, WAVE_MAPPER, &wf, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        g_wo = NULL;
        free(data);
        return -1;
    }
    g_wh.lpData         = data;
    g_wh.dwBufferLength = (DWORD)sz;
    if (waveOutPrepareHeader(g_wo, &g_wh, sizeof(g_wh)) != MMSYSERR_NOERROR ||
        waveOutWrite(g_wo, &g_wh, sizeof(g_wh)) != MMSYSERR_NOERROR) {
        snd_win_stop();
        return -1;
    }
    g_pcm = data;
    return 0;
}
#endif

void playSoundNopcm(const char* filePath) {
    size_t len;
    int isPcm;
    int isOpus;

    if (!filePath || !filePath[0]) {
        return;
    }
    len = strlen(filePath);
    isPcm  = (len >= 4 && strcmp(filePath + len - 4, ".pcm") == 0);
    isOpus = (len >= 5 && strcmp(filePath + len - 5, ".opus") == 0);

#if defined(_WIN32)
    (void)isOpus;
    if (isPcm && snd_win_play_pcm(filePath) == 0) {
        return;
    }
    snd_win_stop();
    PlaySoundA(filePath, NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#elif defined(__APPLE__)
    (void)isPcm; (void)isOpus;
    {
        char* afplay[] = {"afplay", (char*)filePath, NULL};
        snd_posix_stop();
        g_snd_pid = snd_spawn("afplay", afplay);
    }
#else
    snd_posix_stop();
    if (isPcm) {
        char* pcm[] = {"aplay", "-q", "-f", "S16_LE", "-r", "44100", "-c", "1",
                       (char*)filePath, NULL};
        g_snd_pid = snd_spawn("aplay", pcm);
    } else if (isOpus) {
        char* pa[] = {"paplay", (char*)filePath, NULL};
        g_snd_pid = snd_spawn("paplay", pa);
    } else {
        char* pa[] = {"paplay", (char*)filePath, NULL};
        char* ap[] = {"aplay", "-q", (char*)filePath, NULL};
        g_snd_pid = snd_spawn("paplay", pa);
        if (g_snd_pid < 0) {
            g_snd_pid = snd_spawn("aplay", ap);
        }
    }
#endif
}

void playSoundPcm(const char* filePath) {
    static ALCdevice* dev = NULL;
    static ALCcontext* ctx = NULL;
    static ALuint prevBuf = 0, prevSrc = 0;
    ALenum err;

    if (!dev) {
        dev = alcOpenDevice(NULL);
        if (!dev) {
            fprintf(stderr, "playSoundPcm: alcOpenDevice failed\n");
            return;
        }
        ctx = alcCreateContext(dev, NULL);
        if (!ctx) {
            fprintf(stderr, "playSoundPcm: alcCreateContext failed\n");
            alcCloseDevice(dev); dev = NULL;
            return;
        }
        if (!alcMakeContextCurrent(ctx)) {
            fprintf(stderr, "playSoundPcm: alcMakeContextCurrent failed\n");
            alcDestroyContext(ctx); ctx = NULL;
            alcCloseDevice(dev); dev = NULL;
            return;
        }
        {
            const ALCchar* d = alcGetString(NULL, ALC_DEFAULT_ALL_DEVICES_SPECIFIER);
            const ALCchar* a = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
            fprintf(stderr, "playSoundPcm: default device=%s\n", d ? d : "?");
            fprintf(stderr, "playSoundPcm: devices:");
            if (a) {
                while (*a) { fprintf(stderr, " %s", a); a += strlen(a) + 1; }
            }
            fprintf(stderr, "\n");
        }
    }

    if (prevSrc) { alSourceStop(prevSrc); alDeleteSources(1, &prevSrc); prevSrc = 0; }
    if (prevBuf) { alDeleteBuffers(1, &prevBuf); prevBuf = 0; }

    FILE* f = fopen(filePath, "rb");
    if (!f) { fprintf(stderr, "playSoundPcm: fopen failed: %s\n", filePath); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); fprintf(stderr, "playSoundPcm: empty file\n"); return; }
    rewind(f);
    void* data = malloc(sz);
    if (!data) { fclose(f); fprintf(stderr, "playSoundPcm: malloc failed\n"); return; }
    if (fread(data, 1, sz, f) != (size_t)sz) {
        free(data); fclose(f);
        fprintf(stderr, "playSoundPcm: fread failed\n");
        return;
    }
    fclose(f);

    alGenBuffers(1, &prevBuf);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        fprintf(stderr, "playSoundPcm: alGenBuffers error 0x%x\n", err);
        free(data); prevBuf = 0;
        return;
    }

    alGenSources(1, &prevSrc);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        fprintf(stderr, "playSoundPcm: alGenSources error 0x%x\n", err);
        alDeleteBuffers(1, &prevBuf); prevBuf = 0;
        free(data);
        return;
    }

    alListener3f(AL_POSITION, 0, 0, 0);
    alDistanceModel(AL_NONE);
    alSourcef(prevSrc, AL_GAIN, 1.0f);

    alBufferData(prevBuf, AL_FORMAT_MONO16, data, sz, 44100);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        fprintf(stderr, "playSoundPcm: alBufferData error 0x%x (size=%ld)\n", err, sz);
        alDeleteSources(1, &prevSrc); prevSrc = 0;
        alDeleteBuffers(1, &prevBuf); prevBuf = 0;
        free(data);
        return;
    }
    free(data);

    alSourcei(prevSrc, AL_BUFFER, prevBuf);
    alSourcePlay(prevSrc);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        fprintf(stderr, "playSoundPcm: alSourcePlay error 0x%x\n", err);
    }

    {
        ALint state;
        alGetSourcei(prevSrc, AL_SOURCE_STATE, &state);
        fprintf(stderr, "playSoundPcm: source state=0x%x (PLAYING=0x1012)\n", state);
    }
}
