////////////////////////////////////////////////
//
//		pspaalibwav.c
//		Part of the PSP Advanced Audio Library
//		Created by Arshia001
//
//		This file includes functions for playing WAV files
//		with different bitrates/frequencies.
//
////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#include "pspaalibwav.h"

typedef struct
{
    SceUID file;
    char *data;
    int dataLength;
    int dataLocation;
    int dataPos;
    short sigBytes;
    short numChannels;
    int sampleRate;
    int bytesPerSecond;
    int stopReason;
    bool paused;
    bool loadToRam;
    bool autoloop;
    bool initialized;
    bool fromMemory;      /* NEW: true if loaded from memory */
    char *memData;        /* NEW: original memory buffer (if fromMemory) */
    AalibMetadata metadata;
} WavFileInfo;

WavFileInfo streamsWav[32];

bool GetPausedWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    return streamsWav[channel].paused;
}

int SetAutoloopWav(int channel, bool autoloop) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    streamsWav[channel].autoloop = autoloop;
    return PSPAALIB_SUCCESS;
}

int GetStopReasonWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    return streamsWav[channel].stopReason;
}

int PlayWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    streamsWav[channel].paused = FALSE;
    streamsWav[channel].stopReason = PSPAALIB_STOP_NOT_STOPPED;
    return PSPAALIB_SUCCESS;
}

int StopWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    RewindWav(channel);
    streamsWav[channel].paused = TRUE;
    streamsWav[channel].stopReason = PSPAALIB_STOP_ON_REQUEST;
    return PSPAALIB_SUCCESS;
}

int PauseWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    streamsWav[channel].paused = !streamsWav[channel].paused;
    streamsWav[channel].stopReason = PSPAALIB_STOP_NOT_STOPPED;
    return PSPAALIB_SUCCESS;
}

int SeekWav(int time, int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_WAV_UNINITIALIZED_CHANNEL;
    }
    int dataPos = (int)time * streamsWav[channel].bytesPerSecond;
    if (dataPos > streamsWav[channel].dataLength) {
        return PSPAALIB_ERROR_WAV_INVALID_SEEK_TIME;
    }
    streamsWav[channel].dataPos = dataPos;
    if (!streamsWav[channel].loadToRam && !streamsWav[channel].fromMemory) {
        sceIoLseek(streamsWav[channel].file, streamsWav[channel].dataLocation + streamsWav[channel].dataPos, PSP_SEEK_SET);
    }
    return PSPAALIB_SUCCESS;
}

int RewindWav(int channel) {
    return SeekWav(0, channel);
}

int GetBufferWav(short *buf, int length, float amp, int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (streamsWav[channel].paused || !streamsWav[channel].initialized || streamsWav[channel].stopReason == PSPAALIB_STOP_END_OF_STREAM) {
        memset((char *)buf, 0, 4 * length);
        return PSPAALIB_WARNING_PAUSED_BUFFER_REQUESTED;
    }
    int i, index;
    int realLength = length * streamsWav[channel].sigBytes * streamsWav[channel].numChannels * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE;
    if (streamsWav[channel].dataPos + realLength >= streamsWav[channel].dataLength) {
        RewindWav(channel);
        if (!streamsWav[channel].autoloop) {
            streamsWav[channel].paused = TRUE;
            streamsWav[channel].stopReason = PSPAALIB_STOP_END_OF_STREAM;
            memset((char *)buf, 0, 4 * length);
            return PSPAALIB_SUCCESS;
        }
        return GetBufferWav(buf, length, amp, channel);
    }
    if (streamsWav[channel].loadToRam) {
        for (i = 0;i < length;i++) {
            if (streamsWav[channel].sigBytes == 1) {
                index = streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE) + streamsWav[channel].dataPos;
                buf[2 * i] = (streamsWav[channel].data[index] << 8) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (streamsWav[channel].data[index] << 8) * amp;
            } else if (streamsWav[channel].sigBytes == 2) {
                index = streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE) + (streamsWav[channel].dataPos / 2);
                buf[2 * i] = (((short *)streamsWav[channel].data)[index]) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (((short *)streamsWav[channel].data)[index]) * amp;
            } else {
                memset((char *)buf, 0, 4 * length);
                return PSPAALIB_WARNING_WAV_INVALID_SBPS;
            }
        }
    } else if (streamsWav[channel].fromMemory) {
        /* Read from in-memory buffer */
        int startPos = streamsWav[channel].dataPos;
        for (i = 0;i < length;i++) {
            if (streamsWav[channel].sigBytes == 1) {
                index = startPos + streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE);
                buf[2 * i] = (streamsWav[channel].memData[index] << 8) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (streamsWav[channel].memData[index] << 8) * amp;
            } else if (streamsWav[channel].sigBytes == 2) {
                index = startPos + streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE);
                buf[2 * i] = (((short *)(streamsWav[channel].memData + startPos))[index / 2]) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (((short *)(streamsWav[channel].memData + startPos))[index / 2]) * amp;
            } else {
                memset((char *)buf, 0, 4 * length);
                return PSPAALIB_WARNING_WAV_INVALID_SBPS;
            }
        }
    } else {
        sceIoRead(streamsWav[channel].file, streamsWav[channel].data, realLength);
        for (i = 0;i < length;i++) {
            if (streamsWav[channel].sigBytes == 1) {
                index = streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE);
                buf[2 * i] = (streamsWav[channel].data[index] << 8) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (streamsWav[channel].data[index] << 8) * amp;
            } else if (streamsWav[channel].sigBytes == 2) {
                index = streamsWav[channel].numChannels * (int)(i * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE);
                buf[2 * i] = (((short *)streamsWav[channel].data)[index]) * amp;
                index += ((streamsWav[channel].numChannels > 1) ? 1 : 0);
                buf[2 * i + 1] = (((short *)streamsWav[channel].data)[index]) * amp;
            } else {
                memset((char *)buf, 0, 4 * length);
                return PSPAALIB_WARNING_WAV_INVALID_SBPS;
            }
        }
    }
    streamsWav[channel].dataPos += realLength;
    return PSPAALIB_SUCCESS;
}

static void ParseListChunk(WavFileInfo *wav, char *data, int size) {
    if (size < 4) return;

    char type[5];
    memcpy(type, data, 4);
    type[4] = '\0';

    if (!strcmp(type, "INFO")) {
        int pos = 4;
        while (pos < size) {
            char id[5];
            memcpy(id, data + pos, 4);
            id[4] = '\0';
            pos += 4;

            int chunkSize = *(int *)(data + pos);
            pos += 4;

            if (chunkSize <= 0 || pos + chunkSize > size) break;

            char value[256];
            int copySize = chunkSize < 255 ? chunkSize : 254;
            memcpy(value, data + pos, copySize);
            value[copySize] = '\0';

            ConvertStringToUTF8(value, sizeof(value));

            while (copySize > 0 && (value[copySize - 1] == ' ' || value[copySize - 1] == '\n' || value[copySize - 1] == '\r')) {
                value[copySize - 1] = '\0';
                copySize--;
            }

            if (strcmp(id, "INAM") == 0) {
                strncpy(wav->metadata.title, value, 255);
                wav->metadata.title[255] = '\0';
            } else if (strcmp(id, "IART") == 0) {
                strncpy(wav->metadata.artist, value, 255);
                wav->metadata.artist[255] = '\0';
            } else if (strcmp(id, "IPRD") == 0) {
                strncpy(wav->metadata.album, value, 255);
                wav->metadata.album[255] = '\0';
            } else if (strcmp(id, "ICRD") == 0) {
                strncpy(wav->metadata.year, value, 15);
                wav->metadata.year[15] = '\0';
            } else if (strcmp(id, "IGNR") == 0) {
                strncpy(wav->metadata.genre, value, 127);
                wav->metadata.genre[127] = '\0';
            } else if (strcmp(id, "ICMT") == 0) {
                strncpy(wav->metadata.comment, value, 511);
                wav->metadata.comment[511] = '\0';
            }

            pos += chunkSize;
            if (chunkSize % 2 != 0) pos++;
        }
    }
}

static int FindWavChunk(SceUID file, const char *chunkId, int *chunkSize, int *chunkPos) {
    char temp[4];
    int size;
    int filePos = 12;

    while (1) {
        if (sceIoLseek(file, filePos, PSP_SEEK_SET) != filePos) break;
        if (sceIoRead(file, temp, 4) != 4) break;
        if (sceIoRead(file, &size, 4) != 4) break;

        int match = 1;
        match = (memcmp(temp, chunkId, 4) == 0);

        if (match) {
            *chunkSize = size;
            *chunkPos = filePos + 8;
            return 1;
        }

        filePos += (size + 8 + (size & 1));
    }

    return 0;
}

/* Find a chunk in an in-memory WAV buffer */
static int FindWavChunkMem(const unsigned char *data, int dataSize, int startPos, const char *chunkId, int *chunkSize, int *chunkPos) {
    int pos = startPos;
    while (pos + 8 <= dataSize) {
        if (memcmp(data + pos, chunkId, 4) == 0) {
            *chunkSize = *(int *)(data + pos + 4);
            *chunkPos = pos + 8;
            return 1;
        }
        int size = *(int *)(data + pos + 4);
        pos += (size + 8 + (size & 1));
    }
    return 0;
}

int GetMetadataWav(int channel, AalibMetadata *metadata) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsWav[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }

    *metadata = streamsWav[channel].metadata;
    return PSPAALIB_SUCCESS;
}

#define COVER_SEARCH_SIZE (2*1024*1024)
#define READ_BUFFER_SIZE 4096

static void ParseWavCover(SceUID file, WavFileInfo *wav) {
    int currentPos = sceIoLseek(file, 0, PSP_SEEK_CUR);
    int fileSize = sceIoLseek(file, 0, PSP_SEEK_END);
    int searchStart = (fileSize > COVER_SEARCH_SIZE) ? fileSize - COVER_SEARCH_SIZE : 0;

    unsigned char *buffer = malloc(READ_BUFFER_SIZE);
    if (!buffer) return;

    int foundPos = -1;
    const char *target = "image/";
    int targetLen = 6;

    for (int pos = searchStart; pos < fileSize - targetLen; pos += READ_BUFFER_SIZE - targetLen) {
        int readSize = (fileSize - pos) < READ_BUFFER_SIZE ? (fileSize - pos) : READ_BUFFER_SIZE;
        sceIoLseek(file, pos, PSP_SEEK_SET);
        sceIoRead(file, buffer, readSize);

        for (int i = 0; i < readSize - targetLen; i++) {
            if (memcmp(buffer + i, target, targetLen) == 0) {
                foundPos = pos + i;
                goto found_marker;
            }
        }
    }

found_marker:
    free(buffer);

    if (foundPos == -1) {
        sceIoLseek(file, currentPos, PSP_SEEK_SET);
        return;
    }

    sceIoLseek(file, foundPos, PSP_SEEK_SET);

    unsigned char header[24];
    sceIoRead(file, header, sizeof(header));

    enum { UNKNOWN, PNG, JPEG, BMP } imgType = UNKNOWN;
    int sigOffset = 0;

    if (memcmp(header, "\x89PNG\r\n\x1a\n", 8) == 0) {
        imgType = PNG;
    } else if (header[0] == 0xFF && header[1] == 0xD8) {
        imgType = JPEG;
    } else if (memcmp(header, "BM", 2) == 0) {
        imgType = BMP;
    } else {
        for (sigOffset = 1; sigOffset < 64; sigOffset++) {
            if (memcmp(header + sigOffset, "\x89PNG\r\n\x1a\n", 8) == 0) {
                imgType = PNG;
                break;
            }
            if (header[sigOffset] == 0xFF && header[sigOffset + 1] == 0xD8) {
                imgType = JPEG;
                break;
            }
            if (memcmp(header + sigOffset, "BM", 2) == 0) {
                imgType = BMP;
                break;
            }
        }
    }

    if (imgType == UNKNOWN) {
        sceIoLseek(file, currentPos, PSP_SEEK_SET);
        return;
    }

    int imageStart = foundPos + sigOffset;
    int imageSize = fileSize - imageStart;

    unsigned char *imageData = malloc(imageSize);
    if (!imageData) {
        sceIoLseek(file, currentPos, PSP_SEEK_SET);
        return;
    }

    sceIoLseek(file, imageStart, PSP_SEEK_SET);
    sceIoRead(file, imageData, imageSize);

    wav->metadata.cover = g2dTexLoad(NULL, imageData, imageSize, G2D_VOID);
    if (wav->metadata.cover) {
        wav->metadata.has_cover = 1;
    }

    free(imageData);
    sceIoLseek(file, currentPos, PSP_SEEK_SET);
}

int LoadWav(char *filename, int channel, bool loadToRam) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (streamsWav[channel].initialized) {
        UnloadWav(channel);
    }

    memset(&streamsWav[channel].metadata, 0, sizeof(AalibMetadata));
    streamsWav[channel].metadata.has_cover = 0;
    streamsWav[channel].fromMemory = FALSE;
    streamsWav[channel].memData = NULL;

    streamsWav[channel].loadToRam = loadToRam;
    streamsWav[channel].file = sceIoOpen(filename, PSP_O_RDONLY, 0777);
    if (streamsWav[channel].file <= 0) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    char temp[4];
    int size;

    if (sceIoRead(streamsWav[channel].file, temp, 4) != 4 || memcmp(temp, "RIFF", 4) != 0) {
        sceIoClose(streamsWav[channel].file);
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    sceIoRead(streamsWav[channel].file, &size, 4);
    if (sceIoRead(streamsWav[channel].file, temp, 4) != 4 || memcmp(temp, "WAVE", 4) != 0) {
        sceIoClose(streamsWav[channel].file);
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    int fmtSize, fmtPos;
    if (!FindWavChunk(streamsWav[channel].file, "fmt ", &fmtSize, &fmtPos)) {
        sceIoClose(streamsWav[channel].file);
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    sceIoLseek(streamsWav[channel].file, fmtPos, PSP_SEEK_SET);
    short compressionCode;
    sceIoRead(streamsWav[channel].file, &compressionCode, 2);
    if (compressionCode != 0 && compressionCode != 1) {
        sceIoClose(streamsWav[channel].file);
        return PSPAALIB_ERROR_WAV_COMPRESSED_FILE;
    }

    sceIoRead(streamsWav[channel].file, &streamsWav[channel].numChannels, 2);
    sceIoRead(streamsWav[channel].file, &streamsWav[channel].sampleRate, 4);
    sceIoRead(streamsWav[channel].file, &streamsWav[channel].bytesPerSecond, 4);
    sceIoLseek(streamsWav[channel].file, 2, PSP_SEEK_CUR);
    sceIoRead(streamsWav[channel].file, &streamsWav[channel].sigBytes, 2);
    streamsWav[channel].sigBytes >>= 3;

    int dataSize, dataPos;
    if (!FindWavChunk(streamsWav[channel].file, "data", &dataSize, &dataPos)) {
        sceIoClose(streamsWav[channel].file);
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }
    streamsWav[channel].dataLength = dataSize;
    streamsWav[channel].dataLocation = dataPos;
    streamsWav[channel].dataPos = 0;

    ParseWavCover(streamsWav[channel].file, &streamsWav[channel]);

    if (loadToRam) {
        streamsWav[channel].data = (char *)malloc(dataSize);
        if (!streamsWav[channel].data) {
            sceIoClose(streamsWav[channel].file);
            return PSPAALIB_ERROR_WAV_INSUFFICIENT_RAM;
        }
        sceIoLseek(streamsWav[channel].file, dataPos, PSP_SEEK_SET);
        sceIoRead(streamsWav[channel].file, streamsWav[channel].data, dataSize);
        sceIoClose(streamsWav[channel].file);
    } else {
        streamsWav[channel].data = (char *)malloc(1024 * streamsWav[channel].sigBytes *
            streamsWav[channel].numChannels * streamsWav[channel].sampleRate / PSP_SAMPLE_RATE);
        if (!streamsWav[channel].data) {
            sceIoClose(streamsWav[channel].file);
            return PSPAALIB_ERROR_WAV_INSUFFICIENT_RAM;
        }
        sceIoLseek(streamsWav[channel].file, dataPos, PSP_SEEK_SET);
    }

    streamsWav[channel].initialized = TRUE;
    streamsWav[channel].paused = TRUE;
    streamsWav[channel].stopReason = PSPAALIB_STOP_JUST_LOADED;

    return PSPAALIB_SUCCESS;
}

/*
 * Load a WAV file from a memory buffer (e.g. extracted from a ZIP archive).
 * The buffer must remain valid for the lifetime of the channel (we do NOT
 * copy it, to save memory).
 *
 * If loadToRam is TRUE, the audio data is copied into a separate buffer
 * (so the original memory can be freed). If FALSE, we reference the
 * original buffer directly (fromMemory = TRUE).
 */
int LoadWavFromMemory(const unsigned char *data, int dataSize, int channel, bool loadToRam) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    if (data == NULL || dataSize < 44) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }
    if (streamsWav[channel].initialized) {
        UnloadWav(channel);
    }

    memset(&streamsWav[channel].metadata, 0, sizeof(AalibMetadata));
    streamsWav[channel].metadata.has_cover = 0;
    streamsWav[channel].file = -1;
    streamsWav[channel].fromMemory = TRUE;
    streamsWav[channel].memData = (char *)data;   /* cast away const; we only read */
    streamsWav[channel].loadToRam = loadToRam;

    /* Check RIFF header */
    if (memcmp(data, "RIFF", 4) != 0) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }
    if (memcmp(data + 8, "WAVE", 4) != 0) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    /* Find fmt chunk starting from offset 12 */
    int fmtSize, fmtPos;
    if (!FindWavChunkMem(data, dataSize, 12, "fmt ", &fmtSize, &fmtPos)) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }
    if (fmtPos + 16 > dataSize) {
        return PSPAALIB_ERROR_WAV_INVALID_FILE;
    }

    short compressionCode = *(short *)(data + fmtPos);
    if (compressionCode != 0 && compressionCode != 1) {
        return PSPAALIB_ERROR_WAV_COMPRESSED_FILE;
    }

    streamsWav[channel].numChannels   = *(short *)(data + fmtPos + 2);
    streamsWav[channel].sampleRate    = *(int *)(data + fmtPos + 4);
    streamsWav[channel].bytesPerSecond = *(int *)(data + fmtPos + 8);
    streamsWav[channel].sigBytes      = *(short *)(data + fmtPos + 14) >> 3;

    /* Find data chunk. Start search after fmt chunk. */
    int searchStart = fmtPos + fmtSize + (fmtSize & 1);
    int dataSize2, dataPos;
    if (!FindWavChunkMem(data, dataSize, searchStart, "data", &dataSize2, &dataPos)) {
        /* Fall back: search from offset 12 */
        if (!FindWavChunkMem(data, dataSize, 12, "data", &dataSize2, &dataPos)) {
            return PSPAALIB_ERROR_WAV_INVALID_FILE;
        }
    }

    streamsWav[channel].dataLength = dataSize2;
    streamsWav[channel].dataLocation = dataPos;
    streamsWav[channel].dataPos = 0;

    if (dataPos + dataSize2 > dataSize) {
        /* Truncated file - clamp */
        streamsWav[channel].dataLength = dataSize - dataPos;
    }

    if (loadToRam) {
        streamsWav[channel].data = (char *)malloc(streamsWav[channel].dataLength);
        if (!streamsWav[channel].data) {
            streamsWav[channel].initialized = FALSE;
            return PSPAALIB_ERROR_WAV_INSUFFICIENT_RAM;
        }
        memcpy(streamsWav[channel].data, data + dataPos, streamsWav[channel].dataLength);
        streamsWav[channel].fromMemory = FALSE;
        streamsWav[channel].memData = NULL;
    } else {
        /* Reference the original buffer directly */
        streamsWav[channel].data = (char *)(data + dataPos);
        streamsWav[channel].fromMemory = TRUE;
        streamsWav[channel].memData = (char *)data;
        /* We use dataPos as an offset into the original buffer */
        streamsWav[channel].dataPos = 0;
    }

    streamsWav[channel].initialized = TRUE;
    streamsWav[channel].paused = TRUE;
    streamsWav[channel].stopReason = PSPAALIB_STOP_JUST_LOADED;

    return PSPAALIB_SUCCESS;
}

int UnloadWav(int channel) {
    if ((channel < 0) || (channel > 31)) {
        return PSPAALIB_ERROR_WAV_INVALID_CHANNEL;
    }
    StopWav(channel);
    if (!streamsWav[channel].loadToRam && !streamsWav[channel].fromMemory) {
        if (streamsWav[channel].file > 0) {
            sceIoClose(streamsWav[channel].file);
        }
    }
    if (streamsWav[channel].data) {
        /* Only free if we own it */
        if (!streamsWav[channel].fromMemory) {
            free(streamsWav[channel].data);
        }
        streamsWav[channel].data = NULL;
    }
    streamsWav[channel].initialized = FALSE;
    streamsWav[channel].stopReason = PSPAALIB_STOP_UNLOADED;
    streamsWav[channel].fromMemory = FALSE;
    streamsWav[channel].memData = NULL;
    return PSPAALIB_SUCCESS;
}

#ifdef __cplusplus
}
#endif