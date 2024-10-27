// hostmm_ng.c
// This implements EAS Host Wrapper, using C runtime library, with better performance

#include "eas_host.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Only for debugging LED, vibrate, and backlight functions */
#include "eas_report.h"

#if __has_include(<byteswap.h>)
#include <byteswap.h>
#define bswap16 __bswap_16
#define bswap32 __bswap_32
#elif defined(_MSC_VER)
#define bswap16 _byteswap_ushort
#define bswap32 _byteswap_ulong
#elif defined(__GNUC__) || defined(__clang__)
#define bswap16 __builtin_bswap16
#define bswap32 __builtin_bswap32
#endif

// https://stackoverflow.com/a/2103095
// CC BY-SA 3.0
enum {
    O32_LITTLE_ENDIAN = 0x03020100ul,
    O32_BIG_ENDIAN = 0x00010203ul,
    O32_PDP_ENDIAN = 0x01000302ul, /* DEC PDP-11 (aka ENDIAN_LITTLE_WORD) */
    O32_HONEYWELL_ENDIAN = 0x02030001ul /* Honeywell 316 (aka ENDIAN_BIG_WORD) */
};

static const union {
    uint8_t bytes[4];
    uint32_t value;
} o32_host_order = { { 0, 1, 2, 3 } };

#define O32_HOST_ORDER (o32_host_order.value)
// --- end

typedef struct eas_hw_file_tag {
    FILE* handle;
    EAS_BOOL own;
} EAS_HW_FILE;

EAS_RESULT EAS_HWInit(EAS_HW_DATA_HANDLE* pHWInstData)
{
    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWShutdown(EAS_HW_DATA_HANDLE hwInstData)
{
    return EAS_SUCCESS;
}

void* EAS_HWMalloc(EAS_HW_DATA_HANDLE hwInstData, EAS_I32 size)
{
    return malloc((size_t)size);
}

void EAS_HWFree(EAS_HW_DATA_HANDLE hwInstData, void* p)
{
    free(p);
}

void* EAS_HWMemCpy(void* dest, const void* src, EAS_I32 amount)
{
    return memcpy(dest, src, (size_t)amount);
}

void* EAS_HWMemSet(void* dest, int val, EAS_I32 amount)
{
    return memset(dest, val, (size_t)amount);
}

EAS_I32 EAS_HWMemCmp(const void* s1, const void* s2, EAS_I32 amount)
{
    return (EAS_I32)memcmp(s1, s2, (size_t)amount);
}

EAS_RESULT EAS_HWOpenFile(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_LOCATOR locator, EAS_FILE_HANDLE* pFile, EAS_FILE_MODE mode)
{
    if (pFile == NULL) {
        return EAS_ERROR_INVALID_PARAMETER;
    }
    *pFile = malloc(sizeof(EAS_HW_FILE));
    (*pFile)->handle = locator->handle;
    (*pFile)->own = EAS_FALSE;
    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWReadFile(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, void* pBuffer, EAS_I32 n, EAS_I32* pBytesRead)
{
    /* make sure we have a valid handle */
    if (file->handle == NULL) {
        return EAS_ERROR_INVALID_HANDLE;
    }

    size_t count = fread(pBuffer, 1, n, file->handle);
    if (pBytesRead != NULL) {
        *pBytesRead = count;
    }

    if (feof(file->handle)) {
        return EAS_EOF;
    }
    if (ferror(file->handle)) {
        return EAS_ERROR_FILE_READ_FAILED;
    }
    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWGetByte(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, void* p)
{
    return EAS_HWReadFile(hwInstData, file, p, 1, NULL);
}

EAS_RESULT EAS_HWGetWord(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, void* p, EAS_BOOL msbFirst)
{
    EAS_U16 word;
    *((EAS_U16*)p) = 0;
    EAS_RESULT result = EAS_HWReadFile(hwInstData, file, &word, 2, NULL);
    if (result != EAS_SUCCESS) {
        return result;
    }

    if (msbFirst ^ (O32_HOST_ORDER == O32_BIG_ENDIAN)) {
        *((EAS_U16*)p) = bswap16(word);
    } else {
        *((EAS_U16*)p) = word;
    }
    return EAS_SUCCESS;
}

/*lint -esym(715, hwInstData) hwInstData available for customer use */
EAS_RESULT EAS_HWGetDWord(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, void* p, EAS_BOOL msbFirst)
{
    uint32_t dword; // EAS_U32 is not uint32_t
    *((EAS_U32*)p) = 0;
    EAS_RESULT result = EAS_HWReadFile(hwInstData, file, &dword, 4, NULL);
    if (result != EAS_SUCCESS) {
        return result;
    }

    if (msbFirst ^ (O32_HOST_ORDER == O32_BIG_ENDIAN)) {
        *((EAS_U32*)p) = bswap32(dword);
    } else {
        *((EAS_U32*)p) = dword;
    }

    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWFilePos(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, EAS_I32* pPosition)
{
    /* make sure we have a valid handle */
    if (file->handle == NULL) {
        return EAS_ERROR_INVALID_HANDLE;
    }

    long pos = ftell(file->handle);
    if (pPosition != NULL) {
        *pPosition = pos;
    }
    return EAS_SUCCESS;
} /* end EAS_HWFilePos */

EAS_RESULT EAS_HWFileSeek(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, EAS_I32 position)
{
    /* make sure we have a valid handle */
    if (file->handle == NULL) {
        return EAS_ERROR_INVALID_HANDLE;
    }

    /* validate new position */
    if (fseek(file->handle, position, SEEK_SET) != 0) {
        return EAS_ERROR_FILE_SEEK;
    }

    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWFileSeekOfs(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, EAS_I32 position)
{
    /* make sure we have a valid handle */
    if (file->handle == NULL) {
        return EAS_ERROR_INVALID_HANDLE;
    }

    if (fseek(file->handle, position, SEEK_CUR) != 0) {
        return EAS_ERROR_FILE_SEEK;
    }

    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWDupHandle(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file, EAS_FILE_HANDLE* pDupFile)
{
    *pDupFile = NULL;
#if defined(__linux__) // todo: implement for other platforms
    char filePath[PATH_MAX];
    char linkPath[PATH_MAX];

    snprintf(linkPath, PATH_MAX, "/proc/self/fd/%d", fileno(file->handle));

    ssize_t len = readlink(linkPath, filePath, PATH_MAX);
    if (len == -1) {
        return EAS_ERROR_INVALID_HANDLE;
    }
    filePath[len] = '\0';

    EAS_HW_FILE* new_file = malloc(sizeof(EAS_HW_FILE));
    new_file->handle = fopen(filePath, "rb"); // always dup as rb, should be okay
    new_file->own = EAS_TRUE;
    if (new_file->handle == NULL) {
        free(new_file);
        return EAS_ERROR_INVALID_HANDLE;
    }

    long pos = ftell(file->handle);
    if (pos == -1) {
        fclose(new_file->handle);
        free(new_file);
        return EAS_ERROR_FILE_POS;
    }
    if (fseek(new_file->handle, pos, SEEK_SET) != 0) {
        fclose(new_file->handle);
        free(new_file);
        return EAS_ERROR_FILE_SEEK;
    }

    *pDupFile = new_file;
    return EAS_SUCCESS;
#endif
}

EAS_RESULT EAS_HWCloseFile(EAS_HW_DATA_HANDLE hwInstData, EAS_FILE_HANDLE file)
{
    /* make sure we have a valid handle */
    if (file->handle == NULL) {
        return EAS_ERROR_INVALID_HANDLE;
    }

    if (file->own) {
        if (fclose(file->handle) != 0) {
            return EAS_ERROR_INVALID_HANDLE;
        }
    }
    
    free(file);

    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWVibrate(EAS_HW_DATA_HANDLE hwInstData, EAS_BOOL state)
{
    EAS_ReportEx(_EAS_SEVERITY_NOFILTER, 0x1a54b6e8, 0x00000001, state);
    return EAS_SUCCESS;
} /* end EAS_HWVibrate */

EAS_RESULT EAS_HWLED(EAS_HW_DATA_HANDLE hwInstData, EAS_BOOL state)
{
    EAS_ReportEx(_EAS_SEVERITY_NOFILTER, 0x1a54b6e8, 0x00000002, state);
    return EAS_SUCCESS;
}

EAS_RESULT EAS_HWBackLight(EAS_HW_DATA_HANDLE hwInstData, EAS_BOOL state)
{
    EAS_ReportEx(_EAS_SEVERITY_NOFILTER, 0x1a54b6e8, 0x00000003, state);
    return EAS_SUCCESS;
}

EAS_BOOL EAS_HWYield(EAS_HW_DATA_HANDLE hwInstData)
{
    // just let it run
    return EAS_FALSE;
}
