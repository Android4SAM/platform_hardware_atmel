/*
 * Copyright (C) 2014 Atmel Android Open Source Project
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
 
#ifndef ANDROID_HEOLAYER_H
#define ANDROID_HEOLAYER_H

#include <utils/Mutex.h>
#include <linux/videodev.h>
#include <utils/Thread.h>

#include "hwcomposerinterface.h"

#define NUM_OF_HEO_BUF 5

namespace android {

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

class heolayer : public HwcomposerInterface
{
public:
    heolayer(uint32_t id);
    virtual ~heolayer();
    virtual status_t initCheck() const;
    virtual status_t reset();
    virtual status_t checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid);
    virtual status_t prepare(const hwc_layer_1_t* displayer, uint32_t displayerid);
    virtual status_t display(hwc_layer_1_t* const displayer);

    status_t    enableUserPtr();
    status_t    disableUserPtr();   //if disableUserPtr, we default use MMAP

protected:
    virtual void onFirstRef();

private:
    //Must initialize with an id
    heolayer();
    status_t    validateLayer();
    status_t    initLCDDisplayPixel();
    status_t    streamOff();
    status_t    streamOn();
    status_t    allocateBufMemory();
    status_t    freeBufMemory();
    status_t    updateWindowInfo();
    status_t    prepareBuffers(bool usePtrEnabled);
    status_t    dqBuf();

    int32_t     mLayerFd;
    int32_t     mSyncFd;
    status_t    mInit;
    uint32_t    mBufIndex;
    uint32_t    mBufNums;
    size_t*     mBufSize;
    void**      mBufVirAddr;
    uint32_t    mQueuedBufCount;
    uint32_t    mSrcBufWidth;
    uint32_t    mSrcBufHeight;
    int32_t     mVideoId;
    uint32_t    mLCDDisplayWidth;
    uint32_t    mLCDDisplayHeight;
    window_info_t    mWindowInfo;
    bool        mStreamStatus;
    enum v4l2_memory mV4l2Memory;
    bool        mUsePtrEnabled;

    class DqbufThread : public Thread {
        public:
            DqbufThread(wp<heolayer> parent);
            virtual bool threadLoop();
        private:
            wp<heolayer> mParent;
    };
    sp<DqbufThread> mDqbufThread;

    // protected by mLock
    mutable Mutex   mLock;
};
}; // namespace android

#endif // ANDROID_HEOLAYER_H

