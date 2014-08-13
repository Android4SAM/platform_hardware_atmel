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
 
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <fcntl.h>
#include <stdio.h>
#include <cutils/log.h>
#include <sys/ioctl.h>

#include "overlayer.h"

namespace android {

overlayer::overlayer(uint32_t id)
    : mLayerFd(-1),
      mInit(UNKNOWN_ERROR),
      mBufIndex(0),
      mBufNums(NUM_OF_LAYER_BUF),
      mBufSize(0),
      mBufVirAddr(NULL),
      mBufBase(NULL),
      mFbId(-1),
      mLCDDisplayWidth(0),
      mLCDDisplayHeight(0),
      mTranspOffset(24)
{
    char name[64];
    char const * const device_template = "/dev/graphics/fb%u";

    // window & FB & ovrlayer maping
    // win-id:0      -> fb1 -> ovrlayer 1
    // win-id:1      -> fb2 -> ovrlayer2
    // NUM_OF_FB -> fb0 -> baselayer
    snprintf(name, 64, device_template, id);

    mLayerFd = open(name, O_RDWR);

    if(mLayerFd < 0) {
		ALOGE("%s::Failed to open window device (%s) : %s",
				__func__, strerror(errno), name); 
    }

    mFbId = id;
    mBufIndex = mBufNums - 1;
}

void overlayer::onFirstRef()
{
    if(initFixScreenInfo() != NO_ERROR) {
        ALOGE("%s::Failed to init FixScreenInfo", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    if(initVarScreenInfo() != NO_ERROR) {
        ALOGE("%s::Failed to init VarScreenInfo", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    if(validateLayer() != NO_ERROR) {
        ALOGE("%s::Layer not validate", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    if(initLCDDisplayPixel() != NO_ERROR) {
        ALOGE("%s::Failed to init LCDDisplayPixel", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    if(checkSupportBufNums() != NO_ERROR) {
        ALOGE("%s::Failed to check SupportBufNums", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }
    
    if(reset() != NO_ERROR) {
        ALOGE("%s::Failed to reset Pos", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    if(allocateBufMemory() != NO_ERROR) {
        ALOGE("%s::Failed to allocate BufMemory", __func__);
        mInit = UNKNOWN_ERROR;
        return;
    }

    showScreenInfo();

    ALOGD("fb%d initial successful", mFbId);
    mInit = OK;
}

overlayer::~overlayer()
{
    freeBufMemory();
    
    if(mLayerFd >= 0)
        close(mLayerFd);

    ALOGD("fb%d ~free", mFbId);
}

status_t overlayer::initCheck() const
{
    return mInit;
}

//Check whether the displayer could be handle or not
//if could be handled, store the displayer id
status_t overlayer::checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid)
{
    uint32_t layerid;
    if(getDisplayer(layerid)) {
        return ALREADY_EXISTS;
    }
        
    private_handle_t *prev_handle = (private_handle_t *)(displayer->handle);
    hwc_rect_t crop = displayer->sourceCrop;
    hwc_rect_t dst = displayer->displayFrame;
    
    status_t err = HwcomposerInterface::checkFormat(displayer, displayerid);

    if(err != NO_ERROR) {
        ALOGV("%s:: checkFormat error", __func__);
        storeDisplayerStatus(false, displayerid);
        return BAD_VALUE;
    }

    switch (prev_handle->iFormat) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            //Atmel overlayer don't support scaler
            if(getRectWidth(crop) != getRectWidth(dst) ||
               getRectHeight(crop) != getRectHeight(dst)) {
               ALOGV("%s::don't support scaler now", __func__);
               err = BAD_VALUE;
            } else {
                err = NO_ERROR;
            }
            break;
        default :
            ALOGV("%s:: format not support", __func__);
            err = BAD_VALUE;
    }

    if(err != NO_ERROR) {
        storeDisplayerStatus(false, displayerid);
    } else {
        storeDisplayerStatus(true, displayerid);
    }
    
    return err;
}

//Store displayer's coordinates and width/height
//but not update the layer attribute now, will be updated when display
status_t overlayer::prepare(const hwc_layer_1_t * displayer,uint32_t displayerid)
{
    uint32_t layerid;
    if(!getDisplayer(layerid)) {
        ALOGE("%s:: could not handle this layer %d", __func__, displayerid);
        return NO_INIT;
    }

    if(layerid != displayerid) {
        ALOGE("%s:: only could handle layer %d, but not %d", __func__, layerid, displayerid);
        return BAD_VALUE;
    }

    private_handle_t *prev_handle = (private_handle_t *)(displayer->handle);
    hwc_rect_t *visible_rect = (hwc_rect_t *)displayer->visibleRegionScreen.rects;
    uint32_t x,y,w,h;
    
    mVarScreenInfo.bits_per_pixel = prev_handle->uiBpp;
    x = _MAX(visible_rect->left, 0);
    y = _MAX(visible_rect->top, 0);
    w = _MIN(visible_rect->right - x, mLCDDisplayWidth);
    h = _MIN(visible_rect->bottom - y, mLCDDisplayHeight);

    mWindowInfo.updated = false;

    if((x != mWindowInfo.x) || (y != mWindowInfo.y) ||
       (w != mWindowInfo.w) || (h != mWindowInfo.h) ||
       (prev_handle->iFormat != mWindowInfo.format)) {
        mWindowInfo.x = x;
        mWindowInfo.y = y;
        mWindowInfo.w = w;
        mWindowInfo.h = h;
        mWindowInfo.format = prev_handle->iFormat;
        switch (prev_handle->iFormat) {
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_BGRA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
                mTranspOffset = 24;
                break;
        }
        mWindowInfo.updated = true;
    }

    return NO_ERROR;
}

//Copy the data to overlayer buffer
//update layer's attribute is needed
status_t overlayer::display(hwc_layer_1_t * const displayer)
{
    mBufIndex = (mBufIndex + 1) % mBufNums;

    private_handle_t *prev_handle = (private_handle_t *)(displayer->handle);
    hwc_rect_t *cur_rect = (hwc_rect_t *)displayer->visibleRegionScreen.rects;
    uint8_t *dst_addr = (uint8_t *)mBufVirAddr[mBufIndex];
    uint8_t *src_addr = (uint8_t *)prev_handle->base;
    uint32_t cpy_size = 0;

    uint32_t t = displayer->sourceCrop.top;
    uint32_t l = displayer->sourceCrop.left;
    uint32_t offset = (t * prev_handle->stride + l) * prev_handle->uiBpp / 8;

    for (unsigned int i = 0; i < displayer->visibleRegionScreen.numRects; i++) {
        cur_rect->right = _MIN(cur_rect->right, mLCDDisplayWidth);
        cur_rect->left = _MAX(cur_rect->left, 0);
        cur_rect->bottom = _MIN(cur_rect->bottom, mLCDDisplayHeight);
        cur_rect->top = _MAX(cur_rect->top, 0);
        int32_t w = getRectWidth(*cur_rect);
        int32_t h = getRectHeight(*cur_rect);

        uint8_t *cur_dst_addr = dst_addr;
        uint8_t *cur_src_addr = &src_addr[((cur_rect->top - displayer->displayFrame.top) *
            (displayer->displayFrame.right - displayer->displayFrame.left) +
            (cur_rect->left - displayer->displayFrame.left)) * (prev_handle->uiBpp / 8)] + offset;

        if (w == prev_handle->stride) {
            cpy_size= w * (prev_handle->uiBpp / 8) * h;
            h = 1;
        } else {
            cpy_size= w * (prev_handle->uiBpp / 8);
        }

        for (int j = 0; j < h ; j++) {
            memcpy(cur_dst_addr, cur_src_addr, cpy_size);
            cur_dst_addr = &cur_dst_addr[cpy_size];
            cur_src_addr = &cur_src_addr[prev_handle->stride * (prev_handle->uiBpp / 8)];
        }

        cur_rect++;
    }

    updateWindowInfo();

    return NO_ERROR;
}

//the layer must be close when reset
//which means display nothing on screen, and don't affect other layers
status_t overlayer::reset()
{
    memset(&mWindowInfo, 0, sizeof(mWindowInfo));
    
    //Kernel driver will set xpos = ypos = yres = xres = cfg9 = 0 if (nonstd == 0)
    mVarScreenInfo.nonstd = 0;

    mVarScreenInfo.activate &= ~FB_ACTIVATE_MASK;
    mVarScreenInfo.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if(ioctl(mLayerFd, FBIOPUT_VSCREENINFO, &mVarScreenInfo) < 0) {
        ALOGE("%s::Failed to reset", __func__);
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}

//==================Private=======================

status_t overlayer::initFixScreenInfo()
{
    if(mLayerFd < 0)
        return BAD_VALUE;

    //clean the data
    memset(&mFixScreenInfo, 0, sizeof(mFixScreenInfo));

    if(ioctl(mLayerFd, FBIOGET_FSCREENINFO, &mFixScreenInfo) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed : %s", strerror(errno));
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}

status_t overlayer::initVarScreenInfo()
{
    if(mLayerFd < 0)
        return BAD_VALUE;

    //clean the data
    memset(&mVarScreenInfo, 0, sizeof(mVarScreenInfo));

    if(ioctl(mLayerFd, FBIOGET_VSCREENINFO, &mVarScreenInfo) < 0) {
        ALOGE("FBIOGET_VSCREENINFO failed : %s", strerror(errno));
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}

//Get baselayer's xres and yres
status_t overlayer::initLCDDisplayPixel()
{
    int32_t lcdfb;
    struct fb_var_screeninfo  lcd_info;
    char const * const name = "/dev/graphics/fb0";

    lcdfb = open(name, O_RDWR);

    if(lcdfb < 0) {
        ALOGE("Cannot open /dev/graphics/fb0");
        return BAD_VALUE;
    }

    //clean the data
    memset(&lcd_info, 0, sizeof(lcd_info));

    if(ioctl(lcdfb, FBIOGET_VSCREENINFO, &lcd_info) < 0) {
        ALOGE("%s:FBIOGET_VSCREENINFO failed : %s", __func__, strerror(errno));
        return INVALID_OPERATION;
    }

    mLCDDisplayWidth = lcd_info.xres;
    mLCDDisplayHeight = lcd_info.yres;

    if(close(lcdfb) < 0) {
        ALOGD("%s: failed to close lcd device: %d", __func__, lcdfb);
        //Don't return a BAD VALUE because we have get the w/h successful
    }

    return NO_ERROR;
}

status_t overlayer::validateLayer()
{
    if(strcmp(mFixScreenInfo.id, "atmel_hlcdfb_ovl")
            && strcmp(mFixScreenInfo.id, "atmel_hlcdfb_bas")) {
        ALOGE("%s is not a atmel lcd fb device", mFixScreenInfo.id);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t overlayer::checkSupportBufNums()
{
    mVarScreenInfo.yres_virtual = mVarScreenInfo.yres * mBufNums;
    mVarScreenInfo.bits_per_pixel = 32;     //MAX for RGBA8888

    mVarScreenInfo.activate &= ~FB_ACTIVATE_MASK;
    mVarScreenInfo.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if (ioctl(mLayerFd, FBIOPUT_VSCREENINFO, &mVarScreenInfo) < 0) {
        ALOGE("%s::could not use %d buffers", __func__, mBufNums);
        return INVALID_OPERATION;
    }

    //Need to init fix screen info agian because it maybe changed after FBIOPUT_VSCREENINFO
    if(initFixScreenInfo() != NO_ERROR) {
        ALOGE("%s::Failed to init FixScreenInfo", __func__);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

//Buf is mapped from fixscreeninfo
//so we cannot dynamic allocate the buf memory
//each bufsize and the total memory size is decide by kernel
status_t overlayer::allocateBufMemory()
{
    mBufSize = mFixScreenInfo.line_length * mVarScreenInfo.yres;

    if(!mFixScreenInfo.smem_start) {
        ALOGE("%s::fb%d, failed to get the reserved memory", __func__, mFbId);
        return NO_MEMORY;
    }

    size_t memSize = roundUpToPageSize(mBufSize * mBufNums);
    mBufBase = mmap(0, memSize, PROT_READ|PROT_WRITE, MAP_SHARED, mLayerFd, 0);

    if(mBufBase == MAP_FAILED) {
        ALOGE("%s::fb%d, error mapping the framebuffer(%s)", __func__, mFbId, strerror(errno));
        return NO_MEMORY;
    }

    if(mBufVirAddr)
        delete [] mBufVirAddr;

    mBufVirAddr = new uint32_t [mBufNums];

    if(!mBufVirAddr) {
        ALOGE("%s::fb%d, cannot allocate buf container", __func__, mFbId);
        return NO_MEMORY;
    }

    for (uint32_t i = 0; i < mBufNums; i++) {
        mBufVirAddr[i] = intptr_t(mBufBase) + (mBufSize * i);
        ALOGD("fb%d: mBufVirAddr[%d] %p", mFbId, i, (void *)mBufVirAddr[i]);
    }

    return NO_ERROR;
}

status_t overlayer::freeBufMemory()
{
    if(mBufBase != NULL) {
        munmap (mBufBase, mBufSize * mBufNums);
        mBufBase = NULL;
        for (uint32_t i = 0; i < mBufNums; i++) {
            mBufVirAddr[i] = intptr_t(NULL);
        }
    }

    if(mBufVirAddr)
        delete [] mBufVirAddr;

    ALOGD("fb%d: BufMemory free", mFbId);

    return NO_ERROR;
}

status_t overlayer::showScreenInfo()
{
    #define PUT(string,value) ALOGD("fb%d %15s: %d", mFbId, string, mVarScreenInfo.value)

    PUT("xres", xres);
    PUT("yres", yres);
    PUT("xres_virtual", xres_virtual);
    PUT("yres_virtual", yres_virtual);
    PUT("bits_per_pixel", bits_per_pixel);
    return NO_ERROR;
}

status_t overlayer::updateWindowInfo()
{
    mVarScreenInfo.yoffset = (mBufSize / mFixScreenInfo.line_length) * mBufIndex;
    
    if(!mWindowInfo.updated) {
        //Just update yoffset
        if(ioctl(mLayerFd, FBIOPAN_DISPLAY, &mVarScreenInfo) < 0) {
            ALOGE("%s:: failed to update yoffset, xoffset %d, yoffset %d", __func__, mVarScreenInfo.xoffset, mVarScreenInfo.yoffset);
            return INVALID_OPERATION;
        }
        return NO_ERROR;
    }

    mVarScreenInfo.xres = mWindowInfo.w;
    mVarScreenInfo.yres = mWindowInfo.h;
    
    mVarScreenInfo.activate &= ~FB_ACTIVATE_MASK;
    mVarScreenInfo.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    mVarScreenInfo.nonstd = mWindowInfo.x << 10 | mWindowInfo.y;

    if(mTranspOffset)
        mVarScreenInfo.accel_flags = 1;
    else
        mVarScreenInfo.accel_flags = 0;

    mVarScreenInfo.nonstd |= 1 << 31;

    if(ioctl(mLayerFd, FBIOPUT_VSCREENINFO, &mVarScreenInfo) < 0) {
        ALOGE("%s::FBIOPUT_VSCREENINFO(fd:%d, w:%d, h:%d) fail",
          		__func__, mLayerFd, mWindowInfo.w, mWindowInfo.h);
        return BAD_VALUE;
    }

    mWindowInfo.updated = false;

    return NO_ERROR;
}
}; // namespace android