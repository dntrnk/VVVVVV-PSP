////////////////////////////////////////////////
//
//		pspaalibogg.h
//		Part of the PSP Advanced Audio Library
//		Created by Arshia001
//
//		This file includes function declarations for
//		pspaalibogg.c.
//
////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _PSPAALIBOGG_H_
#define _PSPAALIBOGG_H_

#include "pspaalibcommon.h"

bool GetPausedOgg(int channel);
int SetAutoloopOgg(int channel, bool autoloop);
int GetStopReasonOgg(int channel);
int PlayOgg(int channel);
int StopOgg(int channel);
int PauseOgg(int channel);
int RewindOgg(int channel);
int SeekOgg(int channel, int time);
int GetBufferOgg(short *buf, int length, float amp, int channel);
int LoadOgg(char *filename, int channel, bool loadToRam);
int LoadOggFromMemory(const unsigned char *data, int dataSize, int channel, bool loadToRam);
int GetOggLoopStart(int channel);
int GetOggLoopLength(int channel);
int SeekOggToLoopStart(int channel);
int UnloadOgg(int channel);
int GetMetadataOgg(int channel, AalibMetadata *metadata);

#endif

#ifdef __cplusplus
}
#endif