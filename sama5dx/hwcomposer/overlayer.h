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
 
#ifndef ANDROID_OVERLAYER_H
#define ANDROID_OVERLAYER_H

#include "linux/fb.h"

#include "hwcomposerinterface.h"

#define NUM_OF_LAYER_BUF 2

namespace android {

// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------

class overlayer : public HwcomposerInterface
{
public:
    overlayer(uint32_t id);
    virtual ~overlayer();
    virtual status_t initCheck() const;
    virtual status_t reset();
    virtual status_t checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid);
    virtual status_t prepare(const hwc_layer_1_t* displayer, uint32_t displayerid);
    virtual status_t display(hwc_layer_1_t* const displayer);

protected:
    virtual void onFirstRef();

private:
    //Must initialize with an id
    overlayer();
    status_t    initFixScreenInfo();
    status_t    initVarScreenInfo();
    status_t    initLCDDisplayPixel();
    status_t    validateLayer();
    status_t    checkSupportBufNums();
    status_t    allocateBufMemory();
    status_t    freeBufMemory();
    status_t    showScreenInfo();
    status_t    updateWindowInfo();

    int32_t     mLayerFd;
    status_t    mInit;
    uint32_t    mBufIndex;
    uint32_t    mBufNums;
    uint32_t    mBufSize;
    uint32_t*   mBufVirAddr;
    void*       mBufBase;
    int32_t     mFbId;
    uint32_t    mLCDDisplayWidth;
    uint32_t    mLCDDisplayHeight;
    uint32_t    mTranspOffset;
    window_info_t   mWindowInfo;
    struct fb_fix_screeninfo mFixScreenInfo;
    struct fb_var_screeninfo mVarScreenInfo;
};
}; // namespace android

#endif // ANDROID_OVERLAYER_H

