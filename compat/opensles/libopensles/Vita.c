/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/** \file SDL.c SDL platform implementation */

#include "sles_allinclusive.h"
#include <vitasdk.h>

/** \brief Called by SDL to fill the next audio output buffer */
IEngine *slEngine;

// The loader points lstcs_sles_log at its log (data/lswtcs/debug/lstcs-log.txt).

#define AUDIO_LOG(...) do { if (lstcs_sles_log) lstcs_sles_log(__VA_ARGS__); } while (0)

uint8_t audio_buffers[SndFile_NUMBUFS][SndFile_BUFSIZE];
#ifdef HAVE_PTHREAD
static void audioThread(void* arg) {
#else
static int audioThread(unsigned int args, void* arg) {
#endif
	int ch = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, SndFile_BUFSIZE / 4, &_opensles_user_freq != NULL ? _opensles_user_freq : 44100, SCE_AUDIO_OUT_MODE_STEREO);
	if (ch < 0) {
		// LSTCS-P: the original ignored this and then fed audio to an
		// invalid port forever. Retry once on the MAIN port before giving up quietly.
		sceClibPrintf("OpenSLES: BGM port open failed (0x%08X), trying MAIN\n", ch);
		AUDIO_LOG("Audio: BGM port open failed (0x%08X), trying MAIN", ch);
		ch = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, SndFile_BUFSIZE / 4, &_opensles_user_freq != NULL ? _opensles_user_freq : 44100, SCE_AUDIO_OUT_MODE_STEREO);
		if (ch < 0) {
			sceClibPrintf("OpenSLES: no audio port available (0x%08X), audio disabled\n", ch);
			AUDIO_LOG("Audio: no audio port available (0x%08X), audio disabled", ch);
#ifdef HAVE_PTHREAD
			return;
#else
			return sceKernelExitDeleteThread(0);
#endif
		}
	}
	sceAudioOutSetConfig(ch, -1, -1, (SceAudioOutMode)-1);
	AUDIO_LOG("Audio: output port %d open, %d Hz", ch, &_opensles_user_freq != NULL ? _opensles_user_freq : 44100);
	
	int vol_stereo[] = {32767, 32767};
	sceAudioOutSetVolume(ch, (SceAudioOutChannelFlag)(SCE_AUDIO_VOLUME_FLAG_L_CH | SCE_AUDIO_VOLUME_FLAG_R_CH), vol_stereo);
	
	int buf_idx = 0;
	
	for (;;) {
		uint8_t *stream = audio_buffers[buf_idx];
		sceClibMemset(stream, 0, (size_t)SndFile_BUFSIZE);
		buf_idx = (buf_idx + 1) % SndFile_NUMBUFS;
		
		// A peek lock would be risky if output mixes are dynamic, so we use SDL_PauseAudio to
		// temporarily disable callbacks during any change to the current output mix, and use a
		// shared lock here
		interface_lock_shared(slEngine);
		COutputMix *outputMix = slEngine->mOutputMix;
		interface_unlock_shared(slEngine);
		if (NULL != outputMix) {
			SLOutputMixExtItf OutputMixExt = &outputMix->mOutputMixExt.mItf;
			IOutputMixExt_FillBuffer(OutputMixExt, stream, (SLuint32)SndFile_BUFSIZE);
		}
		
		sceAudioOutOutput(ch, stream);
	}
	
	return sceKernelExitDeleteThread(0);
}

/** \brief Called during slCreateEngine */

void SDL_open(IEngine *thisEngine)
{
	slEngine = thisEngine;
#ifdef HAVE_PTHREAD
	pthread_t thd;
	pthread_create(&thd, NULL, (void *(*)(void *)) audioThread, NULL);
#else
	// LSTCS-P: high priority (64 = highest for apps) so the mixer isn't starved by the
	// game's loading and render threads. Any core: apps can't normally pin threads to
	// the system core, and asking for it made thread creation fail (no audio at all).
	SceUID thd = sceKernelCreateThread("OpenSLES Playback", &audioThread, 64, 0x10000, 0, 0, NULL);
	if (thd < 0) {
		AUDIO_LOG("Audio: high-priority thread failed (0x%08X), using default priority", thd);
		thd = sceKernelCreateThread("OpenSLES Playback", &audioThread, 0x10000100, 0x10000, 0, 0, NULL);
	}
	if (thd < 0) {
		AUDIO_LOG("Audio: could not create the audio thread (0x%08X)", thd);
		return;
	}
	int started = sceKernelStartThread(thd, 0, NULL);
	AUDIO_LOG("Audio: mixer thread started (%s)", started >= 0 ? "ok" : "FAILED");
#endif
}


/** \brief Called during Object::Destroy */

void SDL_close(void)
{
}
