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
 
#include <cutils/log.h>

#include "hwcomposerinterface.h"
 
namespace android {

HwcomposerInterface::HwcomposerInterface()
{
    mDisplayerValid = false;
}

void HwcomposerInterface::storeDisplayerStatus(bool valid, uint32_t displayerid)
{
    mDisplayerValid = valid;
    mDisplayerId = displayerid;
}

bool HwcomposerInterface::getDisplayer(uint32_t& layerid) const
{
    layerid = mDisplayerId;
    return mDisplayerValid;
}

status_t HwcomposerInterface::checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid)
{
    if(displayer->flags & HWC_SKIP_LAYER || !displayer->handle) {
        ALOGV("%s::is_skip_layer %d handle %x",
                __func__, displayer->flags & HWC_SKIP_LAYER,
                (uint32_t)displayer->handle);
        return BAD_VALUE;
    }

    if(displayer->compositionType == HWC_FRAMEBUFFER_TARGET) {
        ALOGE("%s::layer(%d) is framebuffer target", __func__, displayerid);
        return BAD_VALUE;
    }

    if(displayer->compositionType == HWC_BACKGROUND) {
        ALOGE("%s::layer(%d) is background", __func__, displayerid);
        return BAD_VALUE;
    }

    hwc_rect_t crop = displayer->sourceCrop;
    hwc_rect_t dst = displayer->displayFrame;

    //check here....if we have any resolution constraints
    if(getRectWidth(crop) < 16 || getRectHeight(crop) < 8) {
        ALOGE("%s::width %d, height %d", __func__, getRectWidth(crop), getRectHeight(crop));
        return BAD_VALUE;
    }

    //don't support rotation now
    if(displayer->transform != 0) {
        ALOGE("%s::transform is not 0 (%d)", __func__, displayer->transform);
        return BAD_VALUE;
    }

    if(displayer->visibleRegionScreen.numRects != 1) {
        ALOGE("%s:: numRects is not 1 (%d)", __func__, displayer->visibleRegionScreen.numRects);
        return BAD_VALUE;
    }

    return NO_ERROR;
}
}; // namespace android

