////////////////////////////////////////////////
// pspaalibencoding.c
////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "pspaalibcommon.h"

// Определение кодировки по BOM (Byte Order Mark)
static int DetectEncodingByBOM(const unsigned char *str, int length) {
    if (length >= 3 && str[0] == 0xEF && str[1] == 0xBB && str[2] == 0xBF) {
        return PSPAALIB_ENCODING_UTF8;
    }
    if (length >= 2 && str[0] == 0xFF && str[1] == 0xFE) {
        return PSPAALIB_ENCODING_UTF16LE;
    }
    if (length >= 2 && str[0] == 0xFE && str[1] == 0xFF) {
        return PSPAALIB_ENCODING_UTF16BE;
    }
    return -1;
}

// Проверка, является ли строка валидной UTF-8
static int IsValidUTF8(const char *str, int length) {
    int i = 0;
    while (i < length) {
        if ((str[i] & 0x80) == 0x00) {
            i++;
        } else if ((str[i] & 0xE0) == 0xC0) {
            if (i + 1 >= length || (str[i + 1] & 0xC0) != 0x80) return 0;
            i += 2;
        } else if ((str[i] & 0xF0) == 0xE0) {
            if (i + 2 >= length || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80) return 0;
            i += 3;
        } else if ((str[i] & 0xF8) == 0xF0) {
            if (i + 3 >= length || (str[i + 1] & 0xC0) != 0x80 || (str[i + 2] & 0xC0) != 0x80 || (str[i + 3] & 0xC0) != 0x80) return 0;
            i += 4;
        } else {
            return 0;
        }
    }
    return 1;
}

// Эвристическое определение кодировки
static int DetectEncoding(const char *str, int length) {
    // Проверка BOM
    int bomEncoding = DetectEncodingByBOM((const unsigned char *)str, length);
    if (bomEncoding != -1) return bomEncoding;

    // Проверка UTF-8
    if (IsValidUTF8(str, length)) return PSPAALIB_ENCODING_UTF8;

    // Проверка UTF-16
    if (length >= 2 && (length % 2) == 0) {
        int isLE = 1, isBE = 1;
        for (int i = 0; i < length; i += 2) {
            if (i + 1 >= length) break;
            if (str[i + 1] != 0) isLE = 0;
            if (str[i] != 0) isBE = 0;
            if (!isLE && !isBE) break;
        }
        if (isLE) return PSPAALIB_ENCODING_UTF16LE;
        if (isBE) return PSPAALIB_ENCODING_UTF16BE;
    }

    // Подсчет русских символов CP1251
    int cp1251_score = 0;
    int koi8r_score = 0;
    int iso8859_score = 0;

    for (int i = 0; i < length; i++) {
        unsigned char c = str[i];

        // CP1251: А-Я (0xC0-0xDF), а-я (0xE0-0xFF), Ёё (0xA8, 0xB8)
        if ((c >= 0xC0 && c <= 0xFF) || c == 0xA8 || c == 0xB8) {
            cp1251_score++;
        }

        // KOI8-R: Русские буквы (0xC0-0xFF)
        if (c >= 0xC0 && c <= 0xFF) {
            koi8r_score++;
        }

        // ISO-8859-1: Проверка недопустимых символов
        if (c >= 0x80 && c <= 0x9F) {
            iso8859_score--;
        } else if (c >= 0xA0) {
            iso8859_score++;
        }
    }

    // Определение кодировки
    if (cp1251_score > 0 && cp1251_score >= koi8r_score) {
        return PSPAALIB_ENCODING_CP1251;
    }
    if (koi8r_score > 0 && koi8r_score > cp1251_score) {
        return PSPAALIB_ENCODING_KOI8R;
    }
    if (iso8859_score > 0) {
        return PSPAALIB_ENCODING_ISO8859_1;
    }

    // По умолчанию считаем CP1251, если есть русские символы
    return (cp1251_score > 0) ? PSPAALIB_ENCODING_CP1251 : PSPAALIB_ENCODING_ISO8859_1;
}

// Конвертация из UTF-16 LE/BE в UTF-8
static int ConvertUTF16ToUTF8(char *str, int max_length, int isBigEndian) {
    unsigned short *utf16 = (unsigned short *)str;
    int utf16_len = 0;

    while (utf16[utf16_len] != 0 && (utf16_len * 2) < max_length) {
        utf16_len++;
    }

    unsigned char *utf8 = (unsigned char *)malloc(max_length);
    if (!utf8) return 0;

    int utf8_pos = 0;

    for (int i = 0; i < utf16_len && utf8_pos < max_length - 4; i++) {
        unsigned short wc = utf16[i];

        if (isBigEndian) {
            wc = (wc >> 8) | (wc << 8);
        }

        if (wc < 0x80) {
            utf8[utf8_pos++] = (unsigned char)wc;
        } else if (wc < 0x800) {
            if (utf8_pos + 2 >= max_length) break;
            utf8[utf8_pos++] = 0xC0 | (wc >> 6);
            utf8[utf8_pos++] = 0x80 | (wc & 0x3F);
        } else if (wc >= 0xD800 && wc <= 0xDBFF) {
            if (i + 1 >= utf16_len) break;

            unsigned short wc2 = utf16[i + 1];
            if (isBigEndian) {
                wc2 = (wc2 >> 8) | (wc2 << 8);
            }

            if (wc2 < 0xDC00 || wc2 > 0xDFFF) break;

            unsigned int code = 0x10000 + ((wc & 0x3FF) << 10) + (wc2 & 0x3FF);

            if (utf8_pos + 4 >= max_length) break;
            utf8[utf8_pos++] = 0xF0 | (code >> 18);
            utf8[utf8_pos++] = 0x80 | ((code >> 12) & 0x3F);
            utf8[utf8_pos++] = 0x80 | ((code >> 6) & 0x3F);
            utf8[utf8_pos++] = 0x80 | (code & 0x3F);
            i++;
        } else {
            if (utf8_pos + 3 >= max_length) break;
            utf8[utf8_pos++] = 0xE0 | (wc >> 12);
            utf8[utf8_pos++] = 0x80 | ((wc >> 6) & 0x3F);
            utf8[utf8_pos++] = 0x80 | (wc & 0x3F);
        }
    }

    utf8[utf8_pos] = '\0';
    strncpy(str, (char *)utf8, max_length);
    free(utf8);

    return utf8_pos;
}

// Конвертация из ISO-8859-1 в UTF-8
static int ConvertISO8859ToUTF8(char *str, int max_length) {
    unsigned char *src = (unsigned char *)str;
    unsigned char *dst = (unsigned char *)malloc(max_length);
    if (!dst) return 0;

    int src_len = strlen(str);
    int dst_len = 0;

    for (int i = 0; i < src_len && dst_len < max_length - 1; i++) {
        unsigned char c = src[i];

        if (c < 0x80) {
            dst[dst_len++] = c;
        } else {
            if (dst_len + 2 >= max_length) break;
            dst[dst_len++] = 0xC0 | (c >> 6);
            dst[dst_len++] = 0x80 | (c & 0x3F);
        }
    }

    dst[dst_len] = '\0';
    strncpy(str, (char *)dst, max_length);
    free(dst);

    return dst_len;
}

// Конвертация из CP1251 в UTF-8
static int ConvertCP1251ToUTF8(char *str, int max_length) {
    static const unsigned short cp1251_to_unicode[128] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
        0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x0000, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
        0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
        0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
        0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
        0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
        0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
        0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
        0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
        0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
        0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
        0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F
    };

    unsigned char *src = (unsigned char *)str;
    unsigned char *dst = (unsigned char *)malloc(max_length);
    if (!dst) return 0;

    int src_len = strlen(str);
    int dst_len = 0;

    for (int i = 0; i < src_len && dst_len < max_length - 3; i++) {
        unsigned char c = src[i];

        if (c < 0x80) {
            dst[dst_len++] = c;
        } else {
            unsigned short u = cp1251_to_unicode[c - 0x80];
            if (u == 0) continue;

            if (u < 0x80) {
                dst[dst_len++] = (unsigned char)u;
            } else if (u < 0x800) {
                if (dst_len + 1 >= max_length) break;
                dst[dst_len++] = 0xC0 | (u >> 6);
                dst[dst_len++] = 0x80 | (u & 0x3F);
            } else {
                if (dst_len + 2 >= max_length) break;
                dst[dst_len++] = 0xE0 | (u >> 12);
                dst[dst_len++] = 0x80 | ((u >> 6) & 0x3F);
                dst[dst_len++] = 0x80 | (u & 0x3F);
            }
        }
    }

    dst[dst_len] = '\0';
    strncpy(str, (char *)dst, max_length);
    free(dst);

    return dst_len;
}

// Конвертация KOI8-R в UTF-8
static int ConvertKOI8RToUTF8(char *str, int max_length) {
    static const unsigned short koi8r_to_unicode[128] = {
        0x2500, 0x2502, 0x250C, 0x2510, 0x2514, 0x2518, 0x251C, 0x2524,
        0x252C, 0x2534, 0x253C, 0x2580, 0x2584, 0x2588, 0x258C, 0x2590,
        0x2591, 0x2592, 0x2593, 0x2320, 0x25A0, 0x2219, 0x221A, 0x2248,
        0x2264, 0x2265, 0x00A0, 0x2321, 0x00B0, 0x00B2, 0x00B7, 0x00F7,
        0x2550, 0x2551, 0x2552, 0x0451, 0x2553, 0x2554, 0x2555, 0x2556,
        0x2557, 0x2558, 0x2559, 0x255A, 0x255B, 0x255C, 0x255D, 0x255E,
        0x255F, 0x2560, 0x2561, 0x0401, 0x2562, 0x2563, 0x2564, 0x2565,
        0x2566, 0x2567, 0x2568, 0x2569, 0x256A, 0x256B, 0x256C, 0x00A9,
        0x044E, 0x0430, 0x0431, 0x0446, 0x0434, 0x0435, 0x0444, 0x0433,
        0x0445, 0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E,
        0x043F, 0x044F, 0x0440, 0x0441, 0x0442, 0x0443, 0x0436, 0x0432,
        0x044C, 0x044B, 0x0437, 0x0448, 0x044D, 0x0449, 0x0447, 0x044A,
        0x042E, 0x0410, 0x0411, 0x0426, 0x0414, 0x0415, 0x0424, 0x0413,
        0x0425, 0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E,
        0x041F, 0x042F, 0x0420, 0x0421, 0x0422, 0x0423, 0x0416, 0x0412,
        0x042C, 0x042B, 0x0417, 0x0428, 0x042D, 0x0429, 0x0427, 0x042A
    };

    unsigned char *src = (unsigned char *)str;
    unsigned char *dst = (unsigned char *)malloc(max_length);
    if (!dst) return 0;

    int src_len = strlen(str);
    int dst_len = 0;

    for (int i = 0; i < src_len && dst_len < max_length - 3; i++) {
        unsigned char c = src[i];

        if (c < 0x80) {
            dst[dst_len++] = c;
        } else {
            unsigned short u = koi8r_to_unicode[c - 0x80];

            if (u < 0x80) {
                dst[dst_len++] = (unsigned char)u;
            } else if (u < 0x800) {
                if (dst_len + 1 >= max_length) break;
                dst[dst_len++] = 0xC0 | (u >> 6);
                dst[dst_len++] = 0x80 | (u & 0x3F);
            } else {
                if (dst_len + 2 >= max_length) break;
                dst[dst_len++] = 0xE0 | (u >> 12);
                dst[dst_len++] = 0x80 | ((u >> 6) & 0x3F);
                dst[dst_len++] = 0x80 | (u & 0x3F);
            }
        }
    }

    dst[dst_len] = '\0';
    strncpy(str, (char *)dst, max_length);
    free(dst);

    return dst_len;
}

// Основная функция конвертации
int ConvertStringToUTF8(char *str, int max_length) {
    if (!str || max_length <= 0) return 0;

    int length = strlen(str);
    if (length == 0) return 1;

    int encoding = DetectEncoding(str, length);

    switch (encoding) {
    case PSPAALIB_ENCODING_UTF8:
        return 1;
    case PSPAALIB_ENCODING_UTF16LE:
    case PSPAALIB_ENCODING_UTF16BE:
        return ConvertUTF16ToUTF8(str, max_length, encoding == PSPAALIB_ENCODING_UTF16BE);
    case PSPAALIB_ENCODING_ISO8859_1:
        return ConvertISO8859ToUTF8(str, max_length);
    case PSPAALIB_ENCODING_CP1251:
        return ConvertCP1251ToUTF8(str, max_length);
    case PSPAALIB_ENCODING_KOI8R:
        return ConvertKOI8RToUTF8(str, max_length);
    default:
        return ConvertISO8859ToUTF8(str, max_length);
    }
}

#ifdef __cplusplus
}
#endif