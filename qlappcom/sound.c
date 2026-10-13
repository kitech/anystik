#include "sound.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void playSoundNopcm(const char* filePath) {
    size_t len = strlen(filePath);
    int isPcm  = (len >= 4 && strcmp(filePath + len - 4, ".pcm") == 0);
    int isOpus = (len >= 5 && strcmp(filePath + len - 5, ".opus") == 0);
    char cmd[1024];

#if defined(_WIN32)
    (void)isPcm; (void)isOpus;
    snprintf(cmd, sizeof(cmd),
        "powershell -c (New-Object Media.SoundPlayer '%s').PlaySync()", filePath);
#elif defined(__APPLE__)
    (void)isPcm; (void)isOpus;
    snprintf(cmd, sizeof(cmd), "afplay \"%s\" &", filePath);
#else
    if (isPcm)
        snprintf(cmd, sizeof(cmd),
            "aplay -f S16_LE -r 44100 -c 1 \"%s\" &", filePath);
    else if (isOpus)
        snprintf(cmd, sizeof(cmd), "paplay \"%s\" &", filePath);
    else
        snprintf(cmd, sizeof(cmd),
            "( which paplay >/dev/null 2>&1 && paplay \"%s\" ) || aplay \"%s\" &",
            filePath, filePath);
#endif

    if (system(cmd) == -1)
        fprintf(stderr, "playSoundNopcm: system() failed\n");
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
