////////////////////////////////////////////////
//
//		pspaalibogg.c
//		Part of the PSP Advanced Audio Library
//		Created by Arshia001
//
//		This file includes functions for playing OGG/Vorbis
//		files.
//		The code for this file is based on the source of
//		LightMP3,written by Sayka (sakya_tg@yahoo.it).
//
////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>

#include "pspaalibogg.h"

typedef struct
{
    SceUID file;
    OggVorbis_File oggVorbisFile;
    char *data;
    long dataPos;
    long dataSize;
    int channel;
    short tempBuf[2048];
    short *buf;
    long bufLength;
    int stopReason;
    bool paused;
    bool initialized;
    bool autoloop;
    bool loadToRam;
    int loopStart;    /* sample position for loop start */
    int loopLength;   /* sample count for loop (0 = full track) */
    AalibMetadata metadata;
    SceLwMutexWorkarea mutex;
} OggFileInfo;

OggFileInfo streamsOgg[10];

static void ExtractLoopComments(OggFileInfo *ogg) {
    vorbis_comment *vc = ov_comment(&ogg->oggVorbisFile, -1);
    if (!vc) return;

    ogg->loopStart = 0;
    ogg->loopLength = 0;
    int loopEnd = 0;

    for (int i = 0; i < vc->comments; i++) {
        char *comment = vc->user_comments[i];
        int len = vc->comment_lengths[i];

        char *copy = (char *)malloc(len + 1);
        if (!copy) continue;
        memcpy(copy, comment, len);
        copy[len] = '\0';

        /* Split at '=' */
        char *eq = strchr(copy, '=');
        if (eq) {
            *eq = '\0';
            char *key = copy;
            char *value = eq + 1;

            /* Normalize key: remove - or _ after LOOP */
            char buf[5];
            strlcpy(buf, key, sizeof(buf));
            if (strcasecmp(buf, "LOOP") == 0
                && (key[4] == '_' || key[4] == '-')) {
                memmove(key + 4, key + 5, strlen(key) - 4);
            }

            if (strcasecmp(key, "LOOPSTART") == 0) {
                ogg->loopStart = (int)strtoll(value, NULL, 10);
            } else if (strcasecmp(key, "LOOPLENGTH") == 0) {
                ogg->loopLength = (int)strtoll(value, NULL, 10);
            } else if (strcasecmp(key, "LOOPEND") == 0) {
                loopEnd = (int)strtoll(value, NULL, 10);
            }
        }

        free(copy);
    }

    if (loopEnd > 0) {
        ogg->loopLength = loopEnd - ogg->loopStart;
    }
}

size_t OggCallbackRead(void *buf, size_t length, size_t memBlockSize, void *ch) {
    int *channel = (int *)ch;
    if (streamsOgg[*channel].loadToRam) {
        if (length * memBlockSize + streamsOgg[*channel].dataPos <= streamsOgg[*channel].dataSize) {
            memcpy(buf, streamsOgg[*channel].data + streamsOgg[*channel].dataPos, length * memBlockSize);
            streamsOgg[*channel].dataPos += length * memBlockSize;
            return length * memBlockSize;
        } else {
            int remaining = streamsOgg[*channel].dataSize - streamsOgg[*channel].dataPos;
            memcpy(buf, streamsOgg[*channel].data + streamsOgg[*channel].dataPos, remaining);
            streamsOgg[*channel].dataPos = streamsOgg[*channel].dataSize;
            return remaining;
        }
    } else {
        return sceIoRead(streamsOgg[*channel].file, buf, length * memBlockSize);
    }
}

int OggCallbackSeek(void *ch, ogg_int64_t offset, int position) {
    int *channel = (int *)ch;
    long seekPos;
    switch (position) {
    case PSP_SEEK_SET:
        seekPos = (long)offset;
        break;
    case PSP_SEEK_CUR:
        seekPos = streamsOgg[*channel].dataPos + (long)offset;
        break;
    case PSP_SEEK_END:
        seekPos = streamsOgg[*channel].dataSize + (long)offset;
        break;
    default:
        return 0;
    }
    if (seekPos == 0) {
        if (!streamsOgg[*channel].autoloop) {
            streamsOgg[*channel].paused = TRUE;
            streamsOgg[*channel].stopReason = PSPAALIB_STOP_END_OF_STREAM;
        }
    }
    if (streamsOgg[*channel].loadToRam) {
        streamsOgg[*channel].dataPos = seekPos;
    } else {
        sceIoLseek32(streamsOgg[*channel].file, (unsigned int)offset, position);
    }
    return seekPos;
}

long OggCallbackTell(void *ch) {
    int *channel = (int *)ch;
    if (streamsOgg[*channel].loadToRam) {
        return streamsOgg[*channel].dataPos;
    } else {
        return sceIoLseek32(streamsOgg[*channel].file, 0, SEEK_CUR);
    }
}

int OggCallbackClose(void *ch) {
    int *channel = (int *)ch;
    if (streamsOgg[*channel].loadToRam) {
        free(streamsOgg[*channel].data);
        return TRUE;
    } else {
        return sceIoClose(streamsOgg[*channel].file);
    }
}

bool GetPausedOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    return streamsOgg[channel].paused;
}

int SetAutoloopOgg(int channel, bool autoloop) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    streamsOgg[channel].autoloop = autoloop;
    return PSPAALIB_SUCCESS;
}

int GetStopReasonOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    return streamsOgg[channel].stopReason;
}

int PlayOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    streamsOgg[channel].paused = FALSE;
    streamsOgg[channel].stopReason = PSPAALIB_STOP_NOT_STOPPED;
    return PSPAALIB_SUCCESS;
}

int StopOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    RewindOgg(channel);
    streamsOgg[channel].paused = TRUE;
    streamsOgg[channel].stopReason = PSPAALIB_STOP_ON_REQUEST;
    return PSPAALIB_SUCCESS;
}

int PauseOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    streamsOgg[channel].paused = !streamsOgg[channel].paused;
    streamsOgg[channel].stopReason = PSPAALIB_STOP_NOT_STOPPED;
    return PSPAALIB_SUCCESS;
}

int RewindOgg(int channel) {
    return SeekOgg(channel, 0);
}

int SeekOgg(int channel, int time) {
    sceKernelLockLwMutex(&(streamsOgg[channel].mutex), 1, NULL);
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }
    bool tempPause = streamsOgg[channel].paused;
    streamsOgg[channel].paused = TRUE;
    if (time == 0)
        ov_raw_seek(&(streamsOgg[channel].oggVorbisFile), 1);
    else
        ov_pcm_seek(&(streamsOgg[channel].oggVorbisFile), time * 44100);
    streamsOgg[channel].paused = tempPause;
    sceKernelUnlockLwMutex(&(streamsOgg[channel].mutex), 1);
    return PSPAALIB_SUCCESS;
}

int GetBufferOgg(short *buf, int length, float amp, int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    int byteLength = length << 2;
    if (!streamsOgg[channel].initialized || streamsOgg[channel].paused) {
        memset((char *)buf, 0, byteLength);
        return PSPAALIB_WARNING_PAUSED_BUFFER_REQUESTED;
    }
    sceKernelLockLwMutex(&(streamsOgg[channel].mutex), 1, NULL);
    int currentSection, i;
    while (streamsOgg[channel].bufLength < byteLength) {
        unsigned long bytesToRead = byteLength - streamsOgg[channel].bufLength;
        unsigned long bytesRead = ov_read(&(streamsOgg[channel].oggVorbisFile), (char *)streamsOgg[channel].tempBuf, bytesToRead, &currentSection);
        if (!bytesRead) {
            if (!streamsOgg[channel].autoloop) {
                streamsOgg[channel].paused = TRUE;
                streamsOgg[channel].stopReason = PSPAALIB_STOP_END_OF_STREAM;
                sceKernelUnlockLwMutex(&(streamsOgg[channel].mutex), 1);
                return PSPAALIB_WARNING_END_OF_STREAM_REACHED;
            }
            /* Loop back to loopStart (or 0 if not set) */
            int ls = streamsOgg[channel].loopStart;
            if (ls > 0) {
                ov_pcm_seek(&(streamsOgg[channel].oggVorbisFile), ls);
            } else {
                ov_raw_seek(&(streamsOgg[channel].oggVorbisFile), 0);
            }
        }
        streamsOgg[channel].buf = (short *)realloc(streamsOgg[channel].buf, streamsOgg[channel].bufLength + bytesRead);
        memcpy((void *)streamsOgg[channel].buf + streamsOgg[channel].bufLength, streamsOgg[channel].tempBuf, bytesRead);
        streamsOgg[channel].bufLength += bytesRead;
    }
    for (i = 0;i < 2 * length;i++) {
        buf[i] = streamsOgg[channel].buf[i] * amp;
    }
    streamsOgg[channel].bufLength -= byteLength;
    memmove(streamsOgg[channel].buf, streamsOgg[channel].buf + byteLength, streamsOgg[channel].bufLength);
    sceKernelUnlockLwMutex(&(streamsOgg[channel].mutex), 1);
    return PSPAALIB_SUCCESS;
}

// Таблица для декодирования Base64 (включая URL-safe варианты)
static const int8_t base64_dec_table[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //   0-15
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  //  16-31
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62, -1, 62, -1, 63,  //  32-47 (+, /, -)
    52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,  //  48-63 (0-9)
    -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,  //  64-79 (A-O)
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, 63,  //  80-95 (P-Z, _)
    -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,  //  96-111 (a-o)
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,  // 112-127 (p-z)
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,  // 128-255 (все остальное недопустимо)
};

/**
 * Декодирует строку Base64 в бинарные данные.
 *
 * @param data      Входная строка Base64 (может быть без padding '=').
 * @param in_len    Длина входной строки (если 0, вычисляется автоматически).
 * @param out_len   Указатель на переменную для размера декодированных данных.
 * @return          Указатель на декодированные данные (освободить через free()),
 *                  или NULL при ошибке.
 */
unsigned char *base64_decode(const char *data, size_t in_len, size_t *out_len) {
    // Проверка входных параметров
    if (!data || !out_len) return NULL;
    if (in_len == 0) in_len = strlen(data);
    if (in_len == 0) {
        *out_len = 0;
        return NULL;
    }

    // Пропускаем не-Base64 символы (пробелы, переносы строк и т.д.)
    size_t valid_len = 0;
    for (size_t i = 0; i < in_len; i++) {
        char c = data[i];
        if (c >= 0 && c < 128 && base64_dec_table[(uint8_t)c] != -1) {
            valid_len++;
        }
    }
    if (valid_len == 0) {
        *out_len = 0;
        return NULL;
    }

    // Вычисляем размер выходного буфера
    size_t decoded_len = (valid_len * 3) / 4;
    unsigned char *decoded = (unsigned char *)malloc(decoded_len + 4);  // +4 для перестраховки
    if (!decoded) {
        *out_len = 0;
        return NULL;
    }

    // Декодирование
    uint32_t buffer = 0;
    int bits_collected = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = data[i];
        if (c < 0 || c >= 128) continue;  // Пропускаем не-ASCII символы

        int8_t value = base64_dec_table[(uint8_t)c];
        if (value == -1) continue;  // Пропускаем не-Base64 символы

        buffer = (buffer << 6) | value;
        bits_collected += 6;

        if (bits_collected >= 8) {
            bits_collected -= 8;
            decoded[out_pos++] = (buffer >> bits_collected) & 0xFF;
        }
    }

    // Завершаем обработку
    *out_len = out_pos;
    return decoded;
}

static void ExtractVorbisMetadata(OggFileInfo *ogg) {
    vorbis_comment *comments = ov_comment(&ogg->oggVorbisFile, -1);

    for (int i = 0; i < comments->comments; i++) {
        char *comment = comments->user_comments[i];
        int len = comments->comment_lengths[i];

        // Создаем копию для безопасной обработки
        char *comment_copy = malloc(len + 1);
        if (!comment_copy) continue;

        memcpy(comment_copy, comment, len);
        comment_copy[len] = '\0';

        // Конвертируем в UTF-8
        ConvertStringToUTF8(comment_copy, len + 1);

        // Разделяем ключ и значение
        char *eq = strchr(comment_copy, '=');
        if (!eq) {
            free(comment_copy);
            continue;
        }
        *eq = '\0';
        char *key = comment_copy;
        char *value = eq + 1;

        // Обрабатываем известные теги
        if (strcasecmp(key, "title") == 0) {
            strncpy(ogg->metadata.title, value, 255);
            ogg->metadata.title[255] = '\0';
        } else if (strcasecmp(key, "artist") == 0) {
            strncpy(ogg->metadata.artist, value, 255);
            ogg->metadata.artist[255] = '\0';
        } else if (strcasecmp(key, "album") == 0) {
            strncpy(ogg->metadata.album, value, 255);
            ogg->metadata.album[255] = '\0';
        } else if (strcasecmp(key, "date") == 0) {
            strncpy(ogg->metadata.year, value, 15);
            ogg->metadata.year[15] = '\0';
        } else if (strcasecmp(key, "genre") == 0) {
            strncpy(ogg->metadata.genre, value, 127);
            ogg->metadata.genre[127] = '\0';
        } else if (strcasecmp(key, "description") == 0 || strcasecmp(key, "comment") == 0) {
            strncpy(ogg->metadata.comment, value, 511);
            ogg->metadata.comment[511] = '\0';
        } else if (strcasecmp(key, "metadata_block_picture") == 0) {
            size_t decoded_size = 0;
            unsigned char *decoded_data = base64_decode(value, strlen(value), &decoded_size);
            if (!decoded_data || decoded_size < 32) {
                if (decoded_data) free(decoded_data);
                free(comment_copy);
                continue;
            }

            // Указатель на данные после заголовка
            unsigned char *ptr = decoded_data;
            uint32_t mime_len, desc_len, image_size;

            // Чтение заголовка METADATA_BLOCK_PICTURE (безопасно)
            if (decoded_size < 4) goto cleanup;  // Недостаточно данных для типа
            ptr += 4;  // Пропускаем тип изображения

            if (ptr + 4 > decoded_data + decoded_size) goto cleanup;
            mime_len = *(uint32_t *)ptr;
            ptr += 4;

            if (ptr + mime_len > decoded_data + decoded_size) goto cleanup;
            ptr += mime_len;  // Пропускаем MIME-тип

            if (ptr + 4 > decoded_data + decoded_size) goto cleanup;
            desc_len = *(uint32_t *)ptr;
            ptr += 4;

            if (ptr + desc_len > decoded_data + decoded_size) goto cleanup;
            ptr += desc_len;  // Пропускаем описание

            if (ptr + 16 > decoded_data + decoded_size) goto cleanup;
            ptr += 16;  // Пропускаем параметры изображения

            if (ptr + 4 > decoded_data + decoded_size) goto cleanup;
            image_size = *(uint32_t *)ptr;
            ptr += 4;

            // Проверяем, что данные изображения не выходят за границы
            if (ptr + image_size > decoded_data + decoded_size) goto cleanup;

            // Освобождаем предыдущую обложку (если была)
            if (ogg->metadata.cover) {
                g2dTexFree(&ogg->metadata.cover);
                ogg->metadata.has_cover = 0;
            }

            // Загружаем новую обложку
            ogg->metadata.cover = g2dTexLoad(NULL, ptr, image_size, G2D_VOID);
            if (ogg->metadata.cover) {
                ogg->metadata.has_cover = 1;
            }

        cleanup:
            free(decoded_data);
        }

        free(comment_copy);
    }
}

int GetMetadataOgg(int channel, AalibMetadata *metadata) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_ERROR_OGG_UNINITIALIZED_CHANNEL;
    }

    *metadata = streamsOgg[channel].metadata;
    return PSPAALIB_SUCCESS;
}

int LoadOgg(char *filename, int channel, bool loadToRam) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (streamsOgg[channel].initialized) {
        UnloadOgg(channel);
    }
    loadToRam = FALSE;//////Something wrong with the seek/read function.Can't rewind when on ram.
    streamsOgg[channel].file = sceIoOpen(filename, PSP_O_RDONLY, 0777);
    if (!streamsOgg[channel].file) {
        return PSPAALIB_ERROR_OGG_INVALID_FILE;
    }
    streamsOgg[channel].dataSize = sceIoLseek(streamsOgg[channel].file, 0, PSP_SEEK_END);
    sceIoLseek(streamsOgg[channel].file, 0, PSP_SEEK_SET);
    streamsOgg[channel].channel = channel;
    ov_callbacks oggCallbacks;
    oggCallbacks.read_func = OggCallbackRead;
    oggCallbacks.seek_func = OggCallbackSeek;
    oggCallbacks.close_func = OggCallbackClose;
    oggCallbacks.tell_func = OggCallbackTell;
    if (ov_open_callbacks(&(streamsOgg[channel].channel), &(streamsOgg[channel].oggVorbisFile), NULL, 0, oggCallbacks) < 0) {
        sceIoClose(streamsOgg[channel].file);
        return PSPAALIB_ERROR_OGG_OPEN_CALLBACKS;
    }
    memset(&streamsOgg[channel].metadata, 0, sizeof(AalibMetadata));
    streamsOgg[channel].metadata.has_cover = 0;
    //ExtractVorbisMetadata(&streamsOgg[channel]);
    streamsOgg[channel].loadToRam = loadToRam;
    if (streamsOgg[channel].loadToRam) {
        streamsOgg[channel].data = (char *)malloc(streamsOgg[channel].dataSize);
        if (!streamsOgg[channel].data) {
            ov_clear(&(streamsOgg[channel].oggVorbisFile));
            sceIoClose(streamsOgg[channel].file);
            return PSPAALIB_ERROR_OGG_INSUFFICIENT_RAM;
        }
        sceIoRead(streamsOgg[channel].file, streamsOgg[channel].data, streamsOgg[channel].dataSize);
    }

    // create mutex
    if (sceKernelCreateLwMutex(&(streamsOgg[channel].mutex), "OggMutex", 0, 0, NULL) != 0) {
        ov_clear(&(streamsOgg[channel].oggVorbisFile));
        sceIoClose(streamsOgg[channel].file);
        return PSPAALIB_ERROR_OGG_CREATE_MUTEX;
    }

    streamsOgg[channel].bufLength = 0;
    streamsOgg[channel].paused = TRUE;
    streamsOgg[channel].initialized = TRUE;
    streamsOgg[channel].stopReason = PSPAALIB_STOP_JUST_LOADED;

    return PSPAALIB_SUCCESS;
}

int LoadOggFromMemory(const unsigned char *data, int dataSize, int channel, bool loadToRam) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    if (data == NULL || dataSize <= 0) {
        return PSPAALIB_ERROR_OGG_INVALID_FILE;
    }
    if (streamsOgg[channel].initialized) {
        UnloadOgg(channel);
    }

    /* We always copy into our own buffer, so the caller can free theirs. */
    streamsOgg[channel].data = (char *)malloc(dataSize);
    if (!streamsOgg[channel].data) {
        return PSPAALIB_ERROR_OGG_INSUFFICIENT_RAM;
    }
    memcpy(streamsOgg[channel].data, data, dataSize);
    streamsOgg[channel].dataSize = dataSize;
    streamsOgg[channel].dataPos = 0;
    streamsOgg[channel].loadToRam = TRUE;
    streamsOgg[channel].file = -1;
    streamsOgg[channel].channel = channel;

    ov_callbacks oggCallbacks;
    oggCallbacks.read_func = OggCallbackRead;
    oggCallbacks.seek_func = OggCallbackSeek;
    oggCallbacks.close_func = OggCallbackClose;
    oggCallbacks.tell_func = OggCallbackTell;

    if (ov_open_callbacks(&(streamsOgg[channel].channel), &(streamsOgg[channel].oggVorbisFile), NULL, 0, oggCallbacks) < 0) {
        free(streamsOgg[channel].data);
        streamsOgg[channel].data = NULL;
        return PSPAALIB_ERROR_OGG_OPEN_CALLBACKS;
    }

    memset(&streamsOgg[channel].metadata, 0, sizeof(AalibMetadata));
    streamsOgg[channel].metadata.has_cover = 0;

    /* Parse loop points from comments */
    ExtractLoopComments(&streamsOgg[channel]);

    if (sceKernelCreateLwMutex(&(streamsOgg[channel].mutex), "OggMutex", 0, 0, NULL) != 0) {
        ov_clear(&(streamsOgg[channel].oggVorbisFile));
        free(streamsOgg[channel].data);
        streamsOgg[channel].data = NULL;
        return PSPAALIB_ERROR_OGG_CREATE_MUTEX;
    }

    streamsOgg[channel].bufLength = 0;
    streamsOgg[channel].buf = NULL;
    streamsOgg[channel].paused = TRUE;
    streamsOgg[channel].initialized = TRUE;
    streamsOgg[channel].stopReason = PSPAALIB_STOP_JUST_LOADED;
    streamsOgg[channel].autoloop = FALSE;

    return PSPAALIB_SUCCESS;
}

int UnloadOgg(int channel) {
    if ((channel < 0) || (channel > 9)) {
        return PSPAALIB_ERROR_OGG_INVALID_CHANNEL;
    }
    StopOgg(channel);
    if (!streamsOgg[channel].initialized) {
        return PSPAALIB_SUCCESS;
    }
    streamsOgg[channel].paused = TRUE;
    sceKernelLockLwMutex(&(streamsOgg[channel].mutex), 1, NULL);
    ov_clear(&(streamsOgg[channel].oggVorbisFile));
    sceIoClose(streamsOgg[channel].file);
    streamsOgg[channel].stopReason = PSPAALIB_STOP_UNLOADED;
    streamsOgg[channel].initialized = FALSE;
    sceKernelUnlockLwMutex(&(streamsOgg[channel].mutex), 1);
    sceKernelDeleteLwMutex(&(streamsOgg[channel].mutex));
    return PSPAALIB_SUCCESS;
}

#if FALSE // требуется для FLAC

extern int ogg_stream_init(ogg_stream_state *os, int serialno) {
    if (os) {
        memset(os, 0, sizeof(*os));
        os->body_storage = 4096;
        os->body_data = malloc(os->body_storage);
        if (!os->body_data) return -1;

        os->lacing_storage = 1024;
        os->lacing_vals = malloc(os->lacing_storage * sizeof(*os->lacing_vals));
        os->granule_vals = malloc(os->lacing_storage * sizeof(*os->granule_vals));
        if (!os->lacing_vals || !os->granule_vals) {
            if (os->lacing_vals) free(os->lacing_vals);
            if (os->granule_vals) free(os->granule_vals);
            free(os->body_data);
            return -1;
        }

        os->serialno = serialno;
        return 0;
    }
    return -1;
}

extern int ogg_sync_init(ogg_sync_state *oy) {
    if (oy) {
        memset(oy, 0, sizeof(*oy));
        return 0;
    }
    return -1;
}

extern int ogg_sync_clear(ogg_sync_state *oy) {
    if (oy) {
        if (oy->data) free(oy->data);
        memset(oy, 0, sizeof(*oy));
        return 0;
    }
    return -1;
}

extern char *ogg_sync_buffer(ogg_sync_state *oy, long size) {
    if (!oy) return NULL;

    if (oy->data) {
        // If we already have a buffer, but it's too small, realloc
        if (oy->storage < size) {
            char *tmp = realloc(oy->data, size);
            if (!tmp) return NULL;
            oy->data = tmp;
            oy->storage = size;
        }
    } else {
        // No buffer yet, allocate fresh
        oy->data = malloc(size);
        if (!oy->data) return NULL;
        oy->storage = size;
    }

    return oy->data;
}

extern int ogg_stream_clear(ogg_stream_state *os) {
    if (os) {
        if (os->body_data) free(os->body_data);
        if (os->lacing_vals) free(os->lacing_vals);
        if (os->granule_vals) free(os->granule_vals);
        memset(os, 0, sizeof(*os));
        return 0;
    }
    return -1;
}

#endif

#ifdef __cplusplus
}
#endif