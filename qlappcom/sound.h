#ifndef SOUND_H
#define SOUND_H

#ifdef __cplusplus
extern "C" {
#endif

void playSoundNopcm(const char* filePath);
void playSoundPcm(const char* filePath);

#ifdef __cplusplus
}
#endif

#endif
