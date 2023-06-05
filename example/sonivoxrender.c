/*
 * Copyright (c) 2022-2023 Pedro López-Cabanillas
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <eas.h>
#include <eas_reverb.h>

const char *dls_path = NULL;
EAS_I32 reverb_type = -1;
EAS_I32 reverb_wet = 0;
EAS_DATA_HANDLE mEASDataHandle = NULL;

int Read(void *handle, void *buf, int offset, int size) {
    int ret;

    ret = fseek((FILE *) handle, offset, SEEK_SET);
    if (ret < 0) return 0;

    return fread(buf, 1, size, (FILE *) handle);
}

int Size(void *handle) {
    int ret;

    ret = fseek((FILE *) handle, 0, SEEK_END);
    if (ret < 0) return ret;

    return ftell((FILE *) handle);
}

void shutdownLibrary(void)
{
    if (mEASDataHandle) {
        EAS_RESULT result = EAS_Shutdown(mEASDataHandle);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to deallocate the resources for synthesizer library\n");
        }
    }
}

int initializeLibrary(void)
{
    int ok = EXIT_SUCCESS;

#ifdef __WIN32__
	setmode(fileno(stdout), O_BINARY);
#endif

    EAS_RESULT result = EAS_Init(&mEASDataHandle);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to initialize synthesizer library\n");
        ok = EXIT_FAILURE;
        return ok;
    }

    if (mEASDataHandle == NULL) {
        fprintf(stderr, "Failed to initialize EAS data handle\n");
        ok = EXIT_FAILURE;
        return ok;
    }

    if (dls_path != NULL) {
        EAS_FILE mDLSFile;

        mDLSFile.handle = fopen(dls_path, "rb");
        if (mDLSFile.handle == NULL) {
            fprintf(stderr, "Failed to open %s. error: %s\n", dls_path, strerror(errno));
            ok = EXIT_FAILURE;
            goto cleanup;
        }

        mDLSFile.readAt = Read;
        mDLSFile.size = Size;

        result = EAS_LoadDLSCollection(mEASDataHandle, NULL, &mDLSFile);
        fclose(mDLSFile.handle);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to load DLS file\n");
            ok = EXIT_FAILURE;
            goto cleanup;
        }
    }

    result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_WET, reverb_wet);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to set reverb wet amount");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    EAS_BOOL sw = EAS_TRUE;
    EAS_I32 preset = reverb_type - 1;
    if ( preset >= EAS_PARAM_REVERB_LARGE_HALL && preset <= EAS_PARAM_REVERB_ROOM ) {
        sw = EAS_FALSE;
        result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_PRESET, preset);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set reverb preset");
            ok = EXIT_FAILURE;
            goto cleanup;
        }
    }
    result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_BYPASS, sw);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to set reverb bypass");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    return ok;

cleanup:
    shutdownLibrary();

    return ok;
}

int renderFile(const char *fileName)
{
    EAS_HANDLE mEASStreamHandle = NULL;
    EAS_FILE mEasFile;
    EAS_PCM *mAudioBuffer = NULL;
    EAS_I32 mPCMBufferSize = 0;
    const S_EAS_LIB_CONFIG *mEASConfig;

    int ok = EXIT_SUCCESS;

    mEasFile.handle = fopen(fileName, "rb");
    if (mEasFile.handle == NULL) {
        fprintf(stderr, "Failed to open %s. error: %s\n", fileName, strerror(errno));
        ok = EXIT_FAILURE;
        return ok;
    }

    mEasFile.readAt = Read;
    mEasFile.size = Size;

    EAS_RESULT result = EAS_OpenFile(mEASDataHandle, &mEasFile, &mEASStreamHandle);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to open file\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    if(mEASStreamHandle == NULL) {
        fprintf(stderr, "Failed to initialize EAS stream handle\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    result = EAS_Prepare(mEASDataHandle, mEASStreamHandle);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to prepare EAS data and stream handles\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

	EAS_I32 playLength = 0;
	result = EAS_ParseMetaData(mEASDataHandle, mEASStreamHandle, &playLength);
	if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to parse MIDI file metadata\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

	if (playLength == 0) {
		fprintf(stderr, "MIDI file time length returned 0\n");
        ok = EXIT_FAILURE;
        goto cleanup;
	}

    mEASConfig = EAS_Config();
    if (mEASConfig == NULL) {
        fprintf(stderr, "Failed to get the library configuration\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    mPCMBufferSize = sizeof(EAS_PCM) * mEASConfig->mixBufferSize * mEASConfig->numChannels;
    mAudioBuffer = alloca(mPCMBufferSize);
    if (mAudioBuffer == NULL) {
        fprintf(stderr, "Failed to allocate memory of size: %ld", mPCMBufferSize);
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    while (1) {
        EAS_STATE state;
        EAS_I32 count = -1;

        result = EAS_State(mEASDataHandle, mEASStreamHandle, &state);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to get EAS State\n");
            ok = EXIT_FAILURE;
            break;
        }

        if (state == EAS_STATE_STOPPED || state == EAS_STATE_ERROR) {
            break; /* playback is complete */
        }

        result = EAS_Render(mEASDataHandle, mAudioBuffer, mEASConfig->mixBufferSize, &count);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to render audio\n");
            ok = EXIT_FAILURE;
            break;
        }

        if (count != mEASConfig->mixBufferSize) {
            fprintf(stderr, "Only %ld out of %ld frames rendered\n", count, mEASConfig->mixBufferSize);
            ok = EXIT_FAILURE;
            break;
        }

        fwrite(mAudioBuffer, sizeof(EAS_PCM), mEASConfig->mixBufferSize * mEASConfig->numChannels, stdout);
        fflush(stdout);
    }

cleanup:
    if (mEASStreamHandle) {
        result = EAS_CloseFile(mEASDataHandle, mEASStreamHandle);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to close audio file/stream\n");
            ok = EXIT_FAILURE;
        }
    }

    if (mEasFile.handle != NULL) {
        fclose(mEasFile.handle);
    }
    return ok;
}

int main (int argc, char **argv)
{
    int ok = EXIT_SUCCESS;
    int index, c;

    opterr = 0;

    while ((c = getopt (argc, argv, "hd:r:w:")) != -1) {
        switch (c)
        {
        case 'h':
            fprintf (stderr, "Usage: %s [-h] [-d file.dls] [-r 0..4] [-w 0..32765] file.mid ...\n"\
                        "Render standard MIDI files into raw PCM audio.\n"\
                        "Options:\n"\
                        "\t-h\t\tthis help message.\n"\
                        "\t-d file.dls\tDLS soundfont.\n"\
                        "\t-r n\t\treverb preset: 0=no, 1=large hall, 2=hall, 3=chamber, 4=room.\n"\
                        "\t-w n\t\treverb wet: 0..32765.\n", argv[0]);
            return EXIT_FAILURE;
        case 'd':
            dls_path = optarg;
            break;
        case 'r':
            reverb_type = atoi(optarg);
            if (reverb_type < 0 || reverb_type > 4) {
                fprintf (stderr, "invalid reverb preset: %ld\n", reverb_type);
                return EXIT_FAILURE;
            }
            break;
        case 'w':
            reverb_wet = atoi(optarg);
            if (reverb_wet < 0 || reverb_wet > 32765) {
                fprintf (stderr, "invalid reverb amount: %ld\n", reverb_wet);
                return EXIT_FAILURE;
            }
            break;
        default:
            fprintf (stderr, "unknown option: %c\n", optopt);
            return EXIT_FAILURE;
        }
    }

    ok = initializeLibrary();
    if (ok != EXIT_SUCCESS) {
        return ok;
    }

    for (index = optind; index < argc; index++) {
        ok = renderFile(argv[index]);
        if (ok != EXIT_SUCCESS) {
            break;
        }
    }

    shutdownLibrary();

    return ok;
}
