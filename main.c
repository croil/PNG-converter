#include "return_codes.h"
#include <stdio.h>
#include <stdlib.h>

typedef long long ll;
typedef struct IHDR {
    ll width;
    ll height;
    int colorType;
} header;

int checkPNG(FILE *);

ll sumFourBytes(FILE *);

ll readChunk(FILE *, unsigned char *, ll);

int chunkCheck(const char *, const unsigned char *);

ll checkFile(const char *, struct IHDR *headerStruct);

int checkIHDR(FILE *, struct IHDR *headerStruct);

unsigned char paeth(unsigned char, unsigned char, unsigned char);

_Bool isIEND = 0;
#define ISAL
#if defined(ZLIB)
#include <zlib.h>
#elif defined(LIBDEFLATE)
#include <libdeflate.h>
#elif defined(ISAL)

#include <include/igzip_lib.h>

#endif

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "The parameter (count of parameters) is incorrect");
        return ERROR_INVALID_PARAMETER;
    }

    header headerStruct;

    FILE *src;
    if (!(src = fopen(argv[1], "rb"))) {
        fprintf(stderr, "The system cannot find the file specified.");
        return ERROR_FILE_NOT_FOUND;
    }
    ll size = checkFile(argv[1], &headerStruct);
    fseek(src, 8, SEEK_CUR);
    switch (size) {
        case -1:
            fclose(src);
            fprintf(stderr, "The data is invalid");
            return ERROR_INVALID_DATA;
        case -2:
            fclose(src);
            fprintf(
                    stderr,
                    "Not enough memory resources are available to process this command");
            return ERROR_NOT_ENOUGH_MEMORY;
        case -3:
            fclose(src);
            fprintf(stderr,
                    "Not enough storage is available to complete this operation");
            return ERROR_OUTOFMEMORY;
        default:
            break;
    }
    ll sz = size * sizeof(unsigned char);
    unsigned char *buffer = malloc(sz);
    if (!buffer) {
        fclose(src);
        fprintf(
                stderr,
                "Not enough memory resources are available to process this command");
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    ll position = 0;
    while (!isIEND) {
        ll currentSize = readChunk(src, buffer, position);
        if (currentSize < 0) {
            free(buffer);
            fclose(src);
            fprintf(stderr, "The data is invalid");
            return ERROR_INVALID_DATA;
        }
        position += currentSize;
    }
    fclose(src);
    ll uncomressingSize = headerStruct.height *
                          (headerStruct.width * (headerStruct.colorType + 1) + 1);

    unsigned char *new_buffer = malloc(uncomressingSize * sizeof(unsigned char));
    if (!new_buffer) {
        free(buffer);
        fprintf(
                stderr,
                "Not enough memory resources are available to process this command");
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    int return_code = 0;
#if defined(ZLIB)
    return_code = uncompress((Bytef *)new_buffer, (uLongf *)&uncomressingSize,
                             buffer, sz) != Z_OK;
#elif defined(LIBDEFLATE)
    struct libdeflate_decompressor *decomp = libdeflate_alloc_decompressor();
    if (!decomp) {
      free(buffer);
      free(new_buffer);
      fprintf(stderr,
              "Not enough storage is available to complete this operation");
      return ERROR_OUTOFMEMORY;
    }
    return_code = libdeflate_zlib_decompress(decomp, buffer, sz, new_buffer,
                                             uncomressingSize, NULL);
    libdeflate_free_decompressor(decomp);
#elif defined(ISAL)
    struct inflate_state decompressor;
    isal_inflate_init(&decompressor);
    decompressor.next_in = buffer;
    decompressor.avail_in = sz;
    decompressor.next_out = new_buffer;
    decompressor.avail_out = uncomressingSize;
    decompressor.crc_flag = ISAL_ZLIB;
    return_code = isal_inflate(&decompressor);
#endif
    free(buffer);
    if (return_code) {
        fprintf(stderr, "Can't uncompress buffer");
        free(new_buffer);
        return ERROR_INVALID_DATA;
    }

    int mode = 0;
    ll strLen = headerStruct.width * (headerStruct.colorType + 1) + 1;
    for (int i = 0; i < uncomressingSize; i++) {
        if (i % strLen != 0) {
            _Bool hasLeft = i % strLen > headerStruct.colorType + 1;
            _Bool hasTop = i > strLen;
            switch (mode) {
                case 0x1:
                    if (hasLeft)
                        new_buffer[i] += new_buffer[i - headerStruct.colorType - 1];
                    break;
                case 0x2:
                    if (hasTop)
                        new_buffer[i] += new_buffer[i - strLen];
                    break;
                case 0x3:
                    if (!hasTop && hasLeft) {
                        new_buffer[i] += (new_buffer[i - headerStruct.colorType - 1]) / 2;
                        break;
                    }
                    if (hasTop && !hasLeft) {
                        new_buffer[i] += (new_buffer[i - strLen]) / 2;
                        break;
                    }
                    if (hasTop) {
                        new_buffer[i] += (new_buffer[i - headerStruct.colorType - 1] +
                                          new_buffer[i - strLen]) /
                                         2;
                        break;
                    }
                    break;
                case 0x4:
                    if (!hasTop && hasLeft) {
                        new_buffer[i] +=
                                paeth(new_buffer[i - headerStruct.colorType - 1], 0x0, 0x0);
                        break;
                    }
                    if (hasTop && !hasLeft) {
                        new_buffer[i] += paeth(0x0, new_buffer[i - strLen], 0x0);
                        break;
                    }
                    if (hasTop) {
                        new_buffer[i] +=
                                paeth(new_buffer[i - headerStruct.colorType - 1],
                                      new_buffer[i - strLen],
                                      new_buffer[i - strLen - headerStruct.colorType - 1]);
                    }
                    break;
                default:
                    continue;
            }
        } else {
            mode = new_buffer[i];
            if (mode > 4) {
                fprintf(stderr, "Undefined filter type");
                free(new_buffer);
                return -1;
            }
        }
    }

    FILE *out;
    if (!(out = fopen(argv[2], "wb"))) {
        fprintf(stderr, "The system cannot find the file specified.");
        free(new_buffer);
        return ERROR_FILE_NOT_FOUND;
    }

    fprintf(out, "%s\n", headerStruct.colorType == 2 ? "P6" : "P5");
    fprintf(out, "%d %d\n", (int) headerStruct.width, (int) headerStruct.height);
    fprintf(out, "%d\n", 255);
    for (ll i = 0; i < headerStruct.height; i++)
        if (fwrite(new_buffer + i * strLen + 1, 1, strLen - 1, out) != strLen - 1) {
            fprintf(stderr, "Couldn't write to the file");
            break;
        }

    free(new_buffer);
    fclose(out);
    return ERROR_SUCCESS;
}

unsigned char paeth(unsigned char a, unsigned char b, unsigned char c) {
    int decA = a;
    int decB = b;
    int decC = c;
    int p = decA + decB - decC;
    int pa = abs(p - decA);
    int pb = abs(p - decB);
    int pc = abs(p - decC);
    if (pa <= pb && pa <= pc)
        return a;
    if (pb <= pc)
        return b;
    return c;
}

ll readChunk(FILE *src, unsigned char *p, ll position) {
    ll chunkLength = sumFourBytes(src);
    if (chunkLength < 0)
        return chunkLength;

    unsigned char chunkType[4];
    if (fread(chunkType, 1, 4, src) != 4)
        return -1;

    if (chunkCheck("IDAT", chunkType) == 2) {
        if (fread(p + position, 1, chunkLength, src) != chunkLength)
            return -1;
        fseek(src, 4, SEEK_CUR);
        return chunkLength * (int) sizeof(unsigned char);
    }
    if (chunkCheck("IEND", chunkType) == 2) {
        isIEND = 1;
        fseek(src, chunkLength + 4, SEEK_CUR);
        return 0;
    }
    fseek(src, chunkLength + 4, SEEK_CUR);
    return 0;
}

int chunkCheck(const char *arr, const unsigned char *p) {
    if (p[0] != 'I' && p[0] != 'P') {
        if (p[0] >= 'A' && p[0] <= 'Z') {
            return 0;
        }
        return 1;
    }
    for (int i = 0; i < 4; i++) {
        if (arr[i] != p[i]) {
            return 0;
        }
    }
    return 2;
}

ll checkFile(const char *fileName, struct IHDR *headerStruct) {
    FILE *stream;
    if ((stream = fopen(fileName, "rb")) == NULL) {
        return -3;
    }

    int checkPng = checkPNG(stream);
    if (checkPng != 0) {
        fclose(stream);
        return checkPng;
    }
    int checkIhdr = checkIHDR(stream, headerStruct);
    if (checkIhdr != 0) {
        fclose(stream);
        return checkIhdr;
    }
    int palitre = 0;     // Palitre was found and it's before IDAT;
    int wasIdat = 0;     // IDAT was found;
    int endIDAT = 0;     // IDAT's are consecutive;
    ll sectionSize = 0;  // common size of all idat chunks;
    int correctFile = 0; // IEND was found and it's last;
    while (!feof(stream)) {
        ll chunkLength = sumFourBytes(stream);
        if (chunkLength < 0) {
            break;
        }
        unsigned char chunkType[4];
        if (fread(chunkType, 1, 4, stream) != 4) {
            break;
        }

        int mode = chunkCheck("IDAT", chunkType);
        if (mode == 1) // Non-critical chunk
        {
            if (wasIdat) {
                endIDAT = 1;
            }
            if (palitre == 1) {
                break;
            }
            fseek(stream, chunkLength + 4, SEEK_CUR);
            continue;
        }
        if (mode == 0) // Critical chunk but not IDAT
        {
            if (wasIdat) {
                endIDAT = 1;
            }
            if (palitre == 1) {
                break;
            }
            int isEND = chunkCheck("IEND", chunkType);
            int isPLTE = chunkCheck("PLTE", chunkType);
            if (isEND == 2) {
                fseek(stream, chunkLength + 4, SEEK_CUR);
                unsigned char afterEnd[1];
                if ((fread(afterEnd, 1, 1, stream) == 1) != 1) {
                    correctFile = 1;
                }
                break;
            } else if (isPLTE == 2) {
                palitre = 1;
                if (headerStruct->colorType == 0 || chunkLength % 3 != 0 ||
                    chunkLength < 3 || chunkLength > 3 * 256) {
                    break;
                }
                fseek(stream, chunkLength + 4, SEEK_CUR);
                continue;
            } else {
                fseek(stream, chunkLength + 4, SEEK_CUR);
                break;
            }
        }
        // IDAT checking;
        if (palitre == 1) {
            palitre = 0;
        }
        if (endIDAT) {
            break;
        }
        wasIdat = 1;
        sectionSize += chunkLength;
        fseek(stream, chunkLength + 4, SEEK_CUR);
    }

    fclose(stream);
    if (correctFile == 1 && wasIdat) {
        return sectionSize;
    } else {
        return -1;
    }
}

int checkIHDR(FILE *src, struct IHDR *headerStruct) {
    ll chunkLength = sumFourBytes(src);
    if (chunkLength < 0)
        return -3;
    unsigned char chunkType[4];

    if (fread(chunkType, 1, 4, src) != 4)
        return -3;
    if (chunkCheck("IHDR", chunkType) == 2) {
        headerStruct->width = sumFourBytes(src);
        if (headerStruct->width <= 0 || headerStruct->width > 2147483647) {
            return -1;
        }
        headerStruct->height = sumFourBytes(src);
        if (headerStruct->height <= 0 || headerStruct->height > 2147483647) {
            return -1;
        }
        unsigned char last[5];

        if (fread(last, 1, 5, src) != 5)
            return -3;
        headerStruct->colorType = (int) last[1];
        if ((last[1] == 0x2 || last[1] == 0x0) && last[2] == 0x0 &&
            last[3] == 0x0 && last[4] == 0x0) {
            fseek(src, 4, SEEK_CUR);
            return 0;
        }
        fseek(src, 4, SEEK_CUR);
        return -1;
    } else {
        return -1;
    }
}

int checkPNG(FILE *src) {
    const unsigned char PNG[8] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
    unsigned char png_signature[8];
    if (fread(png_signature, 1, 8, src) != 8) {
        return -3;
    }
    for (int i = 0; i < 8; i++) {
        if (png_signature[i] != PNG[i]) {
            return -1;
        }
    }

    return 0;
}

ll sumFourBytes(FILE *src) {
    unsigned char lengthBuffer[4];
    if (fread(lengthBuffer, 1, 4, src) != 4)
        return -3;
    return (ll) lengthBuffer[0] * 16777216 + (ll) lengthBuffer[1] * 65536 +
           (ll) lengthBuffer[2] * 256 + (ll) lengthBuffer[3];
}