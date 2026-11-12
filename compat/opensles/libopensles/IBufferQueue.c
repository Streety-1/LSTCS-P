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

/* BufferQueue implementation */

#include "sles_allinclusive.h"


/** Determine the state of the audio player or audio recorder associated with a buffer queue.
 *  Note that PLAYSTATE and RECORDSTATE values are equivalent (where PLAYING == RECORDING).
 */

static SLuint32 getAssociatedState(IBufferQueue *this)
{
    SLuint32 state;
    switch (InterfaceToObjectID(this)) {
    case SL_OBJECTID_AUDIOPLAYER:
        state = ((CAudioPlayer *) this->mThis)->mPlay.mState;
        break;
    case SL_OBJECTID_AUDIORECORDER:
        state = ((CAudioRecorder *) this->mThis)->mRecord.mState;
        break;
    default:
        // unreachable, but just in case we will assume it is stopped
        assert(SL_BOOLEAN_FALSE);
        state = SL_PLAYSTATE_STOPPED;
        break;
    }
    return state;
}

/*
 * LSTCS-P: sample conversion for queued PCM.
 *
 * The mixer only understands 16-bit stereo at the output rate (44.1 kHz by default).
 * The original code converted by repeating each sample a *whole* number of times
 * (output_rate / input_rate, integer-divided), so any rate that did not divide evenly
 * played at the wrong speed (32 kHz ~38% fast, 24 kHz ~84% fast) and 48 kHz gave a
 * zero-length buffer (silence). It also kept converted data in one global ring of 32
 * buffers shared by every player and thread: when the ring wrapped it freed buffers
 * that could still be queued or playing (use-after-free -> noise and crashes).
 *
 * Now every queue slot owns its conversion buffer, and conversion uses a 16.16
 * fixed-point step with linear interpolation, so any input rate plays at the right speed.
 */
static inline int16_t read_sample(const uint8_t *src, SLuint32 frame, int channel, int channels, int bps) {
    if (channels == 1) channel = 0;
    if (bps == 8)
        return (int16_t) (((int) src[frame * channels + channel] - 0x80) << 8);
    return ((const int16_t *) src)[frame * channels + channel];
}

static SLuint32 convert_pcm(BufferHeader *slot, const void *pBuffer, SLuint32 size,
                            SLuint32 inRateMilliHz, SLuint32 outRateMilliHz, int channels, int bps) {
    const int bytesPerFrame = channels * (bps / 8);
    if (bytesPerFrame <= 0) return 0;
    const SLuint32 inFrames = size / bytesPerFrame;
    if (inFrames == 0) return 0;
    if (inRateMilliHz == 0) inRateMilliHz = outRateMilliHz;

    SLuint32 outFrames = (SLuint32) (((uint64_t) inFrames * outRateMilliHz + inRateMilliHz - 1) / inRateMilliHz);
    if (outFrames == 0) outFrames = 1;
    const SLuint32 outBytes = outFrames * 4;

    if (slot->mConvCap < outBytes) {
        void *n = realloc(slot->mConv, outBytes);
        if (n == NULL) return 0;
        slot->mConv = n;
        slot->mConvCap = outBytes;
    }

    const uint8_t *src = (const uint8_t *) pBuffer;
    int16_t *dst = (int16_t *) slot->mConv;
    const uint64_t step = ((uint64_t) inRateMilliHz << 16) / outRateMilliHz; // input frames per output frame, 16.16
    uint64_t pos = 0;
    SLuint32 i;
    for (i = 0; i < outFrames; ++i, pos += step) {
        SLuint32 f0 = (SLuint32) (pos >> 16);
        if (f0 >= inFrames) f0 = inFrames - 1;
        SLuint32 f1 = f0 + 1 < inFrames ? f0 + 1 : f0;
        int32_t frac = (int32_t) (pos & 0xFFFF);
        int c;
        for (c = 0; c < 2; ++c) {
            int32_t s0 = read_sample(src, f0, c, channels, bps);
            int32_t s1 = read_sample(src, f1, c, channels, bps);
            dst[i * 2 + c] = (int16_t) (s0 + (((s1 - s0) * frac) >> 16));
        }
    }
    return outBytes;
}

SLresult IBufferQueue_Enqueue(SLBufferQueueItf self, const void *pBuffer, SLuint32 size)
{
    SL_ENTER_INTERFACE
    //SL_LOGV("IBufferQueue_Enqueue(%p, %p, %lu)", self, pBuffer, size);

    // Note that Enqueue while a Clear is pending is equivalent to Enqueue followed by Clear
    
    if (NULL == pBuffer || 0 == size) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBufferQueue *this = (IBufferQueue *) self;
        interface_lock_exclusive(this);
        BufferHeader *oldRear = this->mRear, *newRear;
        if ((newRear = oldRear + 1) == &this->mArray[this->mNumBuffers + 1]) {
            newRear = this->mArray;
        }
        if (newRear == this->mFront) {
            result = SL_RESULT_BUFFER_INSUFFICIENT;
            SLES_STAT_INC(SLES_STAT_DROPPED);
            {
                
                static unsigned rejected = 0;
                if (lstcs_sles_log && (++rejected <= 5 || (rejected % 200) == 0))
                    lstcs_sles_log("Audio: buffer queue full, a sound buffer was dropped (%u so far)", rejected);
            }
        } else {
            SLES_STAT_INC(SLES_STAT_ENQUEUED);
            const SLuint32 outRate = (SLuint32) (&_opensles_user_freq!=NULL?_opensles_user_freq:44100) * 1000;
            const int bps = this->bps == 8 ? 8 : 16;
            const int channels = this->channels == 1 ? 1 : 2;
            if (this->samplerate == outRate && channels == 2 && bps == 16) {
                // Already in mixer format: play the application's buffer directly.
                oldRear->mBuffer = pBuffer;
                oldRear->mSize = size;
            } else {
                SLuint32 outSize = convert_pcm(oldRear, pBuffer, size, this->samplerate, outRate, channels, bps);
                if (outSize == 0) {
                    // Conversion failed (out of memory): queue silence of the right length
                    // rather than garbage, so the game's buffer callbacks keep flowing.
                    oldRear->mBuffer = pBuffer;
                    oldRear->mSize = 0;
                } else {
                    oldRear->mBuffer = oldRear->mConv;
                    oldRear->mSize = outSize;
                }
            }
            this->mRear = newRear;
            ++this->mState.count;
            result = SL_RESULT_SUCCESS;
        }
        // set enqueue attribute if state is PLAYING and the first buffer is enqueued
        interface_unlock_exclusive_attributes(this, ((SL_RESULT_SUCCESS == result) &&
            (1 == this->mState.count) && (SL_PLAYSTATE_PLAYING == getAssociatedState(this))) ?
            ATTR_ENQUEUE : ATTR_NONE);
    }
    SL_LEAVE_INTERFACE
}


SLresult IBufferQueue_Clear(SLBufferQueueItf self)
{
    SL_ENTER_INTERFACE

    result = SL_RESULT_SUCCESS;
    IBufferQueue *this = (IBufferQueue *) self;
    interface_lock_exclusive(this);

#ifdef ANDROID
    if (SL_OBJECTID_AUDIOPLAYER == InterfaceToObjectID(this)) {
        CAudioPlayer *audioPlayer = (CAudioPlayer *) this->mThis;
        // flush associated audio player
        result = android_audioPlayer_bufferQueue_onClear(audioPlayer);
        if (SL_RESULT_SUCCESS == result) {
            this->mFront = &this->mArray[0];
            this->mRear = &this->mArray[0];
            this->mState.count = 0;
            this->mState.playIndex = 0;
            this->mSizeConsumed = 0;
        }
    }
#endif

#ifdef USE_OUTPUTMIXEXT
    // mixer might be reading from the front buffer, so tread carefully here
    // NTH asynchronous cancel instead of blocking until mixer acknowledges
    this->mClearRequested = SL_BOOLEAN_TRUE;
    do {
        interface_cond_wait(this);
    } while (this->mClearRequested);
#endif

    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE
}


static SLresult IBufferQueue_GetState(SLBufferQueueItf self, SLBufferQueueState *pState)
{
    SL_ENTER_INTERFACE

    // Note that GetState while a Clear is pending is equivalent to GetState before the Clear

    if (NULL == pState) {
        result = SL_RESULT_PARAMETER_INVALID;
    } else {
        IBufferQueue *this = (IBufferQueue *) self;
        SLBufferQueueState state;
        interface_lock_shared(this);
#ifdef __cplusplus // FIXME Is this a compiler bug?
        state.count = this->mState.count;
        state.playIndex = this->mState.playIndex;
#else
        state = this->mState;
#endif
        interface_unlock_shared(this);
        *pState = state;
        result = SL_RESULT_SUCCESS;
    }

    SL_LEAVE_INTERFACE
}


SLresult IBufferQueue_RegisterCallback(SLBufferQueueItf self,
    slBufferQueueCallback callback, void *pContext)
{
    SL_ENTER_INTERFACE

    IBufferQueue *this = (IBufferQueue *) self;
    interface_lock_exclusive(this);
    // verify pre-condition that media object is in the SL_PLAYSTATE_STOPPED state
    if (SL_PLAYSTATE_STOPPED == getAssociatedState(this)) {
        this->mCallback = callback;
        this->mContext = pContext;
        result = SL_RESULT_SUCCESS;
    } else {
        result = SL_RESULT_PRECONDITIONS_VIOLATED;
    }
    interface_unlock_exclusive(this);

    SL_LEAVE_INTERFACE
}


static const struct SLBufferQueueItf_ IBufferQueue_Itf = {
    IBufferQueue_Enqueue,
    IBufferQueue_Clear,
    IBufferQueue_GetState,
    IBufferQueue_RegisterCallback
};

void IBufferQueue_init(void *self)
{
    //SL_LOGV("IBufferQueue_init(%p) entering", self);
    IBufferQueue *this = (IBufferQueue *) self;
    this->mItf = &IBufferQueue_Itf;
    this->mState.count = 0;
    this->mState.playIndex = 0;
    this->mCallback = NULL;
    this->mContext = NULL;
    this->mNumBuffers = 0;
    this->mClearRequested = SL_BOOLEAN_FALSE;
    this->mArray = NULL;
    this->mFront = NULL;
    this->mRear = NULL;
#ifdef ANDROID
    this->mSizeConsumed = 0;
#endif
    BufferHeader *bufferHeader = this->mTypical;
    unsigned i;
    for (i = 0; i < BUFFER_HEADER_TYPICAL+1; ++i, ++bufferHeader) {
        bufferHeader->mBuffer = NULL;
        bufferHeader->mSize = 0;
        bufferHeader->mConv = NULL;
        bufferHeader->mConvCap = 0;
    }
}


/** \brief Free the buffer queue, if it was larger than typical.
  * Called by CAudioPlayer_Destroy and CAudioRecorder_Destroy.
  */

void IBufferQueue_Destroy(IBufferQueue *this)
{
    if (NULL != this->mArray) {
        unsigned i;
        for (i = 0; i < (unsigned) this->mNumBuffers + 1; ++i) {
            free(this->mArray[i].mConv);
            this->mArray[i].mConv = NULL;
            this->mArray[i].mConvCap = 0;
        }
    }
    if ((NULL != this->mArray) && (this->mArray != this->mTypical)) {
        free(this->mArray);
        this->mArray = NULL;
    }
}
