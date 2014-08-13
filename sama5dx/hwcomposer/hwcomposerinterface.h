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
 
#ifndef ANDROID_HWCOMPOSERTINTERFACE_H
#define ANDROID_HWCOMPOSERTINTERFACE_H

#include <sys/types.h>
#include <utils/Errors.h>

#include <utils/RefBase.h>
#include <asm/page.h>			// just want PAGE_SIZE define
#include <sys/mman.h>
#include <hardware/hwcomposer.h>
#include <hardware/hardware.h>
#include "gralloc_priv.h"

namespace android {

inline uint32_t roundUpToPageSize(uint32_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

inline int32_t getRectWidth(hwc_rect_t rect) {
    return (rect.right - rect.left);
}

inline int32_t getRectHeight(hwc_rect_t rect) {
    return (rect.bottom - rect.top);
}

inline int32_t _MIN(int32_t x, int32_t y) {
    return ((x < y) ? x : y);
}

inline int32_t _MAX(int32_t x, int32_t y) {
    return ((x > y) ? x : y);
}
// ---------------------------------------------------------------------------

typedef struct window_info {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    int32_t  format;
    bool     updated;
} window_info_t;

// ---------------------------------------------------------------------------

// abstract base class 
class HwcomposerInterface : public RefBase
{
public:
    HwcomposerInterface();
    virtual status_t initCheck() const = 0;
    virtual status_t reset() = 0; //will called if the layer not been used
    virtual status_t checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid);
    virtual void     storeDisplayerStatus(bool valid, uint32_t displayerid);
    virtual bool     getDisplayer(uint32_t& layerid) const;
    virtual status_t prepare(const hwc_layer_1_t* displayer, uint32_t displayerid) = 0;
    virtual status_t display(hwc_layer_1_t* const displayer) = 0;
    
private:
    uint32_t mDisplayerId;
    bool     mDisplayerValid;
};
}; // namespace android

#endif // ANDROID_HWCOMPOSERTINTERFACE_H