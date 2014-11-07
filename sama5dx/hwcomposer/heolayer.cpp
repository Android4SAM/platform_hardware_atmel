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
#include <sync/sw_sync.h>

#include "heolayer.h"
#include "v4l2_utils.h"

namespace android {

heolayer::heolayer(uint32_t id)
    : mLayerFd(-1),
      mSyncFd(-1),
      mInit(UNKNOWN_ERROR),
      mBufIndex(0),
      mBufNums(0),
      mBufSize(NULL),
      mBufVirAddr(NULL),
      mQueuedBufCount(0),
      mSrcBufWidth(0),
      mSrcBufHeight(0),
      mVideoId(-1),
      mLCDDisplayWidth(0),
      mLCDDisplayHeight(0),
      mStreamStatus(false),
      mV4l2Memory(V4L2_MEMORY_MMAP),
      mUsePtrEnabled(false)
{
    char name[64];
    char const * const device_template = "/dev/video%u";

    snprintf(name, 64, device_template, id);

    mLayerFd = open(name, O_RDWR);

    if(mLayerFd < 0) {
		ALOGE("%s::Failed to open window device (%s) : %s",
				__func__, strerror(errno), name); 
    }

    mVideoId = id;
    mBufIndex = NUM_OF_HEO_BUF - 1;
}

void heolayer::onFirstRef()
{
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

    mDqbufThread = new DqbufThread(this);

    ALOGD("video%d initial successful\n", mVideoId);
    mInit = OK;
}

heolayer::~heolayer()
{
    reset();
        
    if(mLayerFd >= 0)
        close(mLayerFd);
    
    ALOGD("video%d ~free",mVideoId);
}

status_t heolayer::initCheck() const
{
    return mInit;
}

status_t heolayer::checkFormat(const hwc_layer_1_t* displayer, uint32_t displayerid)
{
    uint32_t layerid;
    if(getDisplayer(layerid)) {
        return ALREADY_EXISTS;
    }

    private_handle_t *prev_handle = (private_handle_t *)(displayer->handle);
    status_t err = HwcomposerInterface::checkFormat(displayer, displayerid);

    if(err != NO_ERROR) {
        ALOGV("%s:: checkFormat error", __func__);
        storeDisplayerStatus(false, displayerid);
        return BAD_VALUE;
    }

    switch (prev_handle->iFormat) {
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            err = NO_ERROR;
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

status_t heolayer::prepare(const hwc_layer_1_t * displayer,uint32_t displayerid)
{
    uint32_t layerid;
    bool     srcBufSizeUpdate = false;
    bool     v4l2MemoryTypeChanged = false;
    bool     usePtrEnabled = false;

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
        mWindowInfo.updated = true;
    }

    hwc_rect_t scrop = displayer->sourceCrop;

    if((mSrcBufWidth != (uint32_t)getRectWidth(scrop)) ||
       (mSrcBufHeight != (uint32_t)getRectHeight(scrop))) {
        mSrcBufWidth = getRectWidth(scrop);
        mSrcBufHeight = getRectHeight(scrop);
        srcBufSizeUpdate = true;
    }

    if(prev_handle->flags & private_handle_t::PRIV_FLAGS_DMABUFFER)
        usePtrEnabled = true;
    else
        usePtrEnabled = false;

    if (usePtrEnabled != mUsePtrEnabled)
        v4l2MemoryTypeChanged = true;

    if(!mWindowInfo.updated && !srcBufSizeUpdate && !v4l2MemoryTypeChanged)
        return NO_ERROR;

    if(prepareBuffers(usePtrEnabled) != NO_ERROR) {
        ALOGE("%s:: prepare failed", __func__);
        return UNKNOWN_ERROR;
    }

    streamOn();
    
    return NO_ERROR;
}

status_t heolayer::display(hwc_layer_1_t * const displayer)
{
    private_handle_t *prev_handle = (private_handle_t *)(displayer->handle);
    hwc_rect_t *cur_rect = (hwc_rect_t *)displayer->visibleRegionScreen.rects;
    int w = mSrcBufWidth;
    int h = mSrcBufHeight;
    uint32_t cpy_size = 0;

    switch (prev_handle->iFormat) {
    /* Note: The format is HAL_PIXEL_FORMAT_YV12
         * In gralloc.cpp, bpp is set to 2, so uiBpp is 2*8
         * But actually the bpp for this format should be 1.5 (3/2)
         * So here should be ((prev_handle->uiBpp * 3) / (8 * 2 * 2))
         */
    case HAL_PIXEL_FORMAT_YV12:
        cpy_size = w * prev_handle->uiBpp * 3 / (8 * 2 * 2) * h;
        h = 1;
        break;
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
        cpy_size = w * prev_handle->uiBpp / 8 * h;
        h = 1;
        break;
    default :
        ALOGE("%s, heo don't support this format", __func__);
        return BAD_VALUE;
    }

    switch(mV4l2Memory) {
    case V4L2_MEMORY_MMAP:
        for (unsigned int i = 0; i < displayer->visibleRegionScreen.numRects; i++) {
            uint8_t *cur_dst_addr = (uint8_t *)mBufVirAddr[mBufIndex];
            uint8_t *cur_src_addr = (uint8_t *)prev_handle->base;

            for (int j = 0; j < h ; j++) {
                memcpy(cur_dst_addr, cur_src_addr, cpy_size);
                cur_dst_addr = &cur_dst_addr[cpy_size];
                cur_src_addr = &cur_src_addr[(displayer->displayFrame.right - displayer->displayFrame.left) * (prev_handle->uiBpp / 8)];
            }

            cur_rect++;
        }
        break;
    case V4L2_MEMORY_USERPTR:
        break;
    default:
        ALOGE("%s:: V4l2 Memory type not support now", __func__);
        return BAD_VALUE;
    }

    if(__v4l2_overlay_q_buf(mLayerFd, cpy_size, prev_handle->base, mBufIndex, mV4l2Memory) < 0) {
        ALOGE("%s:Failed to Qbuf", __func__);
    } else {
        mDqbufThread->run();
        mQueuedBufCount++;
        displayer->releaseFenceFd = sw_sync_fence_create(mSyncFd, "heolayer", mQueuedBufCount);
    }

    mBufIndex = (mBufIndex + 1) % mBufNums;

    return NO_ERROR;
}

//==================Own Public====================
status_t heolayer::enableUserPtr()
{
    Mutex::Autolock _l(mLock);
    mV4l2Memory = V4L2_MEMORY_USERPTR;
    mUsePtrEnabled = true;
    return NO_ERROR;
}

status_t heolayer::disableUserPtr()
{
    Mutex::Autolock _l(mLock);
    mV4l2Memory = V4L2_MEMORY_MMAP;
    mUsePtrEnabled = false;
    return NO_ERROR;
}

status_t heolayer::reset()
{
    //stop streaming
    streamOff();

    mDqbufThread->requestExit();
    
    //unmap the buffers 
    // request 0 buffer, v4l2 core will free the allocated buffers and return
    freeBufMemory();

    memset(&mWindowInfo, 0, sizeof(mWindowInfo));
    return NO_ERROR;
}

//==================Private=======================
status_t heolayer::validateLayer()
{
    char cardname[32];
    if(__v4l2_overlay_querycap(mLayerFd, cardname) != NO_ERROR) {
        ALOGE("get cardname error");
        return BAD_VALUE;
    }

    if(strcmp(cardname, "Atmel HEO Layer")) {
        ALOGE("%s:not a HEO device", cardname);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

//Get baselayer's xres and yres
status_t heolayer::initLCDDisplayPixel()
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

    if (ioctl(lcdfb, FBIOGET_VSCREENINFO, &lcd_info) < 0) {
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

status_t heolayer::streamOff()
{
    if(!mStreamStatus)
        return NO_ERROR;

    __v4l2_overlay_stream_off(mLayerFd);
    mStreamStatus = false;
    mQueuedBufCount = 0;

    if(mSyncFd >= 0)
        close(mSyncFd);
    
    return NO_ERROR;
}

status_t heolayer::streamOn()
{
    if(mStreamStatus)
        return NO_ERROR;

    __v4l2_overlay_stream_on(mLayerFd);
    mStreamStatus = true;

    mSyncFd = sw_sync_timeline_create();
    if(mSyncFd < 0)
        ALOGW("can't create sw_sync_timeline: this may caused buffer not sync");
    
    return NO_ERROR;
}

status_t heolayer::allocateBufMemory()
{
    int ret;
    uint32_t bufnums = NUM_OF_HEO_BUF;
    
    ret = __v4l2_overlay_set_output_fmt(mLayerFd, mSrcBufWidth, mSrcBufHeight, mWindowInfo.format);

    if(ret) {
        ALOGE("%s:: failed to set output fmt (%d x %d : %d)",
            __func__, mSrcBufWidth, mSrcBufHeight, mWindowInfo.format);
        return BAD_VALUE;
    }

    //We must set output fmt first, then update window info, because kernel dirver require like this
    if(updateWindowInfo() != NO_ERROR) {
        ALOGE("%s:: prepare failed", __func__);
        return UNKNOWN_ERROR;
    }

    ret = __v4l2_overlay_req_buf(mLayerFd, &bufnums, mV4l2Memory);

    if(ret) {
        ALOGE("%s:: failed to req buf (%d)", __func__, bufnums);
        return BAD_VALUE;
    }

    if(bufnums != NUM_OF_HEO_BUF) {
        ALOGW("%s:: actual bufnum(%d) != req bufnum(%d)",
            __func__, bufnums, NUM_OF_HEO_BUF);
    }

    mBufNums = bufnums;

    switch(mV4l2Memory) {
    case V4L2_MEMORY_MMAP:
        mBufVirAddr = new void* [mBufNums];
        mBufSize    = new size_t [mBufNums];
        if(!mBufVirAddr || !mBufSize) {
            ALOGE("%s:: Failed alloc'ing buffer arrays", __func__);
            return NO_MEMORY;
        }

        for (unsigned int j = 0; j < mBufNums; j++) {
            ret = __v4l2_overlay_map_buf(mLayerFd, j, &mBufVirAddr[j], &mBufSize[j]);
            if(ret) {
                ALOGE("%s: Failed mapping buffers", __func__);
                return NO_MEMORY;
            }
            ALOGD("%s:: mapping success, fd:%d, num:%d, buffers:%p, buffers_len:%d", 
                            __func__, mLayerFd, j, mBufVirAddr[j], mBufSize[j]);
        }
        break;
    case V4L2_MEMORY_USERPTR:
        break;
    default:
        ALOGE("%s:: V4l2 Memory type not support now", __func__);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t heolayer::freeBufMemory()
{
    int ret;
    uint32_t bufnums = 0;
    
    switch(mV4l2Memory) {
    case V4L2_MEMORY_MMAP:
        for (unsigned int i = 0; i < mBufNums; i++) {
            __v4l2_overlay_unmap_buf(mBufVirAddr[i], mBufSize[i]);
        }

        if(mBufVirAddr) {
            delete [] mBufVirAddr;
            mBufVirAddr = NULL;
        }
        if(mBufSize) {
            delete [] mBufSize;
            mBufSize = NULL;
        }
        break;
    case V4L2_MEMORY_USERPTR:
        break;
    default:
        ALOGE("%s:: V4l2 Memory type not support now", __func__);
        return BAD_VALUE;
    }

    //V4L2 core will free the buffers if we req 0 buffers after req not zero buffers
    ret = __v4l2_overlay_req_buf(mLayerFd, &bufnums, mV4l2Memory);

    if(ret) {
        ALOGE("%s:: failed to req buf (%d)", __func__, bufnums);
        return BAD_VALUE;
    }

    if(bufnums != 0) {
        ALOGW("%s:: could not free bufs(%d)", __func__, bufnums);
    }

    mBufNums = 0;
    mQueuedBufCount = 0;

    return NO_ERROR;
}

status_t heolayer::updateWindowInfo()
{
    int ret;
    
    if(!mWindowInfo.updated)
        return NO_ERROR;

    streamOff();

    ret = __v4l2_overlay_set_overlay_fmt(mLayerFd,
                                        mWindowInfo.y,
                                        mWindowInfo.x,
                                        mWindowInfo.w,
                                        mWindowInfo.h);
    if(ret) {
        ALOGE("%s:: failed to set overlay fmt [%d %d %d %d]", __func__,
                                        mWindowInfo.y,
                                        mWindowInfo.x,
                                        mWindowInfo.w,
                                        mWindowInfo.h);
        return BAD_VALUE;
    }

    mWindowInfo.updated = false;

    return NO_ERROR;
}

status_t heolayer::prepareBuffers(bool usePtrEnabled)
{
    status_t ret;

    streamOff();
    
    ret = freeBufMemory();
    if(ret != NO_ERROR) {
        ALOGE("%s:: falied to freeBufMemory", __func__);
        return ret;
    }

    if (usePtrEnabled)
        enableUserPtr();
    else
        disableUserPtr();

    ret = allocateBufMemory();

    if(ret != NO_ERROR) {
        ALOGE("%s:: failed to allocateBufMemory", __func__);
        return ret;
    }

    return NO_ERROR;
}

status_t heolayer::dqBuf()
{
    int index;

    if(__v4l2_overlay_dq_buf(mLayerFd, &index, mV4l2Memory) < 0) {
            ALOGE("%s:Failed to DQbuf", __func__);
            return BAD_VALUE;
    } else {
            sw_sync_timeline_inc(mSyncFd, 1);
    }
    return NO_ERROR;
}

heolayer::DqbufThread::DqbufThread(wp < heolayer > parent)
{
    mParent = parent;
}

bool heolayer::DqbufThread::threadLoop()
{
    sp<heolayer> parent = mParent.promote();

    if(parent->dqBuf() != NO_ERROR)
        return false;

    return true;
}

}; // namespace android

