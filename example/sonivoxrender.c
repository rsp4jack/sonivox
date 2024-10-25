/*
 * Copyright (c) 2022-2024 Pedro López-Cabanillas
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
#include <eas_chorus.h>

const char *dls_path = NULL;
EAS_I32 playback_gain = 90;
EAS_I32 reverb_type = 0;
EAS_I32 reverb_wet = 32767;
EAS_I32 reverb_dry = 0;
EAS_I32 chorus_type = 0;
EAS_I32 chorus_level = 32767;
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

    result = EAS_SetVolume(mEASDataHandle, NULL, playback_gain);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to set volume\n");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    EAS_BOOL reverb_bypass = EAS_TRUE;
    EAS_I32 reverb_preset = reverb_type - 1;
    if ( reverb_preset >= EAS_PARAM_REVERB_LARGE_HALL && reverb_preset <= EAS_PARAM_REVERB_ROOM ) {
        reverb_bypass = EAS_FALSE;
        result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_PRESET, reverb_preset);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set reverb preset");
            ok = EXIT_FAILURE;
            goto cleanup;
        }
        result = EAS_SetParameter(mEASDataHandle,
                                  EAS_MODULE_REVERB,
                                  EAS_PARAM_REVERB_WET,
                                  reverb_wet);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set reverb wet amount");
            ok = EXIT_FAILURE;
            goto cleanup;
        }

        result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_DRY, reverb_dry);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set reverb dry amount");
            ok = EXIT_FAILURE;
            goto cleanup;
        }
    }
    result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_REVERB, EAS_PARAM_REVERB_BYPASS, reverb_bypass);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to set reverb bypass");
        ok = EXIT_FAILURE;
        goto cleanup;
    }

    EAS_BOOL chorus_bypass = EAS_TRUE;
    EAS_I32 chorus_preset = chorus_type - 1;
    if ( chorus_preset >= EAS_PARAM_CHORUS_PRESET1 && chorus_preset <= EAS_PARAM_CHORUS_PRESET4 ) {
        chorus_bypass = EAS_FALSE;
        result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_PRESET, chorus_preset);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set chorus preset");
            ok = EXIT_FAILURE;
            goto cleanup;
        }

        result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_LEVEL, chorus_level);
        if (result != EAS_SUCCESS) {
            fprintf(stderr, "Failed to set chorus level");
            ok = EXIT_FAILURE;
            goto cleanup;
        }
    }

    result = EAS_SetParameter(mEASDataHandle, EAS_MODULE_CHORUS, EAS_PARAM_CHORUS_BYPASS, chorus_bypass);
    if (result != EAS_SUCCESS) {
        fprintf(stderr, "Failed to set chorus bypass");
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

    while ((c = getopt (argc, argv, "hd:r:w:n:c:l:v:")) != -1) {
        switch (c)
        {
        case 'h':
            fprintf (stderr, "Usage: %s [-h] [-d file.dls] [-r 0..4] [-w 0..32767] [-n 0..32767] [-c 0..4] [-l 0..32767] [-v 0..100] file.mid ...\n"\
                        "Render standard MIDI files into raw PCM audio.\n"\
                        "Options:\n"\
                        "\t-h\t\tthis help message.\n"\
                        "\t-d file.dls\tDLS soundfont.\n"\
                        "\t-r n\t\treverb preset: 0=no, 1=large hall, 2=hall, 3=chamber, 4=room.\n"\
                        "\t-w n\t\treverb wet: 0..32767.\n"
                        "\t-n n\t\treverb dry: 0..32767.\n"
                        "\t-c n\t\tchorus preset: 0=no, 1..4=presets.\n"
                        "\t-l n\t\tchorus level: 0..32767.\n"
                        "\t-v n\t\tmaster volume: 0..100.\n"
                        , argv[0]);
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
            if (reverb_wet < 0 || reverb_wet > 32767) {
                fprintf (stderr, "invalid reverb wet: %ld\n", reverb_wet);
                return EXIT_FAILURE;
            }
            break;
        case 'n':
            reverb_dry = atoi(optarg);
            if (reverb_dry < 0 || reverb_dry > 32767) {
                fprintf (stderr, "invalid reverb dry: %ld\n", reverb_dry);
                return EXIT_FAILURE;
            }
            break;
        case 'c':
            chorus_type = atoi(optarg);
            if (chorus_type < 0 || chorus_type > 4) {
                fprintf (stderr, "invalid chorus preset: %ld\n", chorus_type);
                return EXIT_FAILURE;
            }
            break;
        case 'l':
            chorus_level = atoi(optarg);
            if (chorus_level < 0 || chorus_level > 32767) {
                fprintf (stderr, "invalid chorus level: %ld\n", chorus_level);
                return EXIT_FAILURE;
            }
            break;
        case 'v':
            playback_gain = atoi(optarg);
            if (playback_gain < 0 || playback_gain > 100) {
                fprintf (stderr, "invalid playback gain: %ld\n", playback_gain);
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
