/*
 * Copyright (C) 2011 The Android Open Source Project
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

/*
 * Contains implementation of a class EmulatedFakeCameraDevice that encapsulates
 * fake camera device.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "SamSensor"

#include <cutils/log.h>
#include "SamCamera.h"
#include "SamSensor.h"
#include "v4l2_utils.h"

#define CHECK(return_value)                                          \
    if (return_value < 0) {                                          \
        ALOGE("%s::%d fail. errno: %s\n",           \
             __FUNCTION__, __LINE__, strerror(errno));      \
        return BAD_VALUE;                                                   \
    }

namespace android {


status_t SamSensor::init()
{
    int ret = NO_ERROR;

    mCamFd = open(SAMSENSOR_PATH, O_RDWR);
    if (mCamFd < 0) {
        ALOGE("ERR(%s):Cannot open sensor (error : %s)\n", __FUNCTION__, strerror(errno));
        return -1;
    }
    mCamId = 0;
    ret = __v4l2_querycap(mCamFd);
    CHECK(ret);

    ret = __v4l2_enuminput(mCamFd, mCamId);
    CHECK(ret);

    ret = __v4l2_s_input(mCamFd, mCamId);
    CHECK(ret);

    return ret;
}

SamSensor::SamSensor(SamCamera* camera_hal)
    : SamSensorBase(camera_hal),
      mHasRealHardware(true)
{
    int fd;
    ALOGV("%s", __FUNCTION__);
    
    fd = open(SAMSENSOR_PATH, O_RDWR);
    if (fd < 0) {
        ALOGE("ERR(%s):open sensor failed", __FUNCTION__);
        mHasRealHardware = false;
        return;
    }

    close(fd);
}

SamSensor::~SamSensor()
{
    ALOGV("%s", __FUNCTION__);
}

bool SamSensor::hasRealHardware()
{
    return mHasRealHardware;
}

/****************************************************************************
 * Sam camera device abstract interface implementation.
 ***************************************************************************/

status_t SamSensor::connectDevice()
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isInitialized()) {
        ALOGE("%s: Sam camera device is not initialized.", __FUNCTION__);
        return EINVAL;
    }
    if (isConnected()) {
        ALOGW("%s: Sam camera device is already connected.", __FUNCTION__);
        return NO_ERROR;
    }

    init();

    /* There is no device to connect to. */
    mState = ECDS_CONNECTED;

    return NO_ERROR;

}

status_t SamSensor::disconnectDevice()
{
    ALOGV("%s", __FUNCTION__);

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        ALOGW("%s: Sam camera device is already disconnected.", __FUNCTION__);
        return NO_ERROR;
    }
    if (isStarted()) {
        ALOGE("%s: Cannot disconnect from the started device.", __FUNCTION__);
        return EINVAL;
    }

    close(mCamFd);

    /* There is no device to disconnect from. */
    mState = ECDS_INITIALIZED;

    return NO_ERROR;

}

status_t SamSensor::startDevice(int width,
                                               int height,
                                               uint32_t pix_fmt)
{
    ALOGV("%s", __FUNCTION__);
    mBufNums = MAX_BUFFERS;

    Mutex::Autolock locker(&mObjectLock);
    if (!isConnected()) {
        ALOGE("%s: Sam camera device is not connected.", __FUNCTION__);
        return EINVAL;
    }
    if (isStarted()) {
        ALOGE("%s: Sam camera device is already started.", __FUNCTION__);
        return EINVAL;
    }

    mState = ECDS_STARTED;

    /* Initialize the base class. */
    status_t ret =
        SamSensorBase::commonStartDevice(width, height, pix_fmt);
    if (ret != NO_ERROR) {
        ALOGE("%s: commonStartDevice failed", __FUNCTION__);
    }

    /* enum_fmt, s_fmt sample */
    ret = __v4l2_enum_fmt(mCamFd, mPixelFormat);
    CHECK(ret);

    ret = __v4l2_s_parm(mCamFd, &m_StreamParm);
    CHECK(ret);

    ret = __v4l2_set_fmt(mCamFd, mFrameWidth, mFrameHeight, mPixelFormat);
    CHECK(ret);

    ret = __v4l2_req_buf(mCamFd, &mBufNums, V4L2_MEMORY_MMAP);
    CHECK(ret);

    mBufVirAddr = new void* [mBufNums];
    mBufSize    = new size_t [mBufNums];
    if(!mBufVirAddr || !mBufSize) {
        ALOGE("%s:: Failed allocating buffer arrays", __FUNCTION__);
        return NO_MEMORY;
    }

    for (unsigned int i = 0; i < mBufNums; i++) {
        ret = __v4l2_map_buf(mCamFd, i, &mBufVirAddr[i], &mBufSize[i]);
        if(ret) {
            ALOGE("%s: Failed mapping buffers", __FUNCTION__);
            return NO_MEMORY;
        }
        ALOGD("%s:: mapping success, fd:%d, num:%d, buffers:%p, buffers_len:%d", 
                            __FUNCTION__, mCamFd, i, mBufVirAddr[i], mBufSize[i]);
        __v4l2_q_buf(mCamFd, mFrameBufferSize, 0, i, V4L2_MEMORY_MMAP);
    }

    ret = __v4l2_stream_on(mCamFd);
    CHECK(ret);

    return ret;
}

status_t SamSensor::stopDevice()
{
    ALOGV("%s", __FUNCTION__);
    int ret;
    uint32_t bufnums = 0;

    Mutex::Autolock locker(&mObjectLock);
    if (!isStarted()) {
        ALOGW("%s: Sam camera device is not started.", __FUNCTION__);
        return NO_ERROR;
    }
    ret = __v4l2_stream_off(mCamFd);
    CHECK(ret);

    for (unsigned int i = 0; i < mBufNums; i++) {
        __v4l2_unmap_buf(mBufVirAddr[i], mBufSize[i]);
    }

    if(mBufVirAddr) {
        delete [] mBufVirAddr;
        mBufVirAddr = NULL;
    }
    if(mBufSize) {
        delete [] mBufSize;
        mBufSize = NULL;
    }

    //V4L2 core will free the buffers if we req 0 buffers after req not zero buffers
    ret = __v4l2_req_buf(mCamFd, &bufnums, V4L2_MEMORY_MMAP);
    CHECK(ret);

    if(bufnums != 0) {
        ALOGW("%s:: could not free bufs(%d)", __FUNCTION__, bufnums);
    }

    mBufNums = 0;

    SamSensorBase::commonStopDevice();
    mState = ECDS_CONNECTED;

    return NO_ERROR;

}


/****************************************************************************
 * Worker thread management overrides.
 ***************************************************************************/
static int cnt = 0;

bool SamSensor::inWorkerThread()
{
    //ALOGV("%s", __FUNCTION__);
    int index;
    /* Wait till FPS timeout expires, or thread exit message is received. */
    WorkerThread::SelectRes res =
        getWorkerThread()->Select(-1, 1000000 / mFPS);
    if (res == WorkerThread::EXIT_THREAD) {
        ALOGV("%s: Worker thread has been terminated.", __FUNCTION__);
        return false;
    }

    __v4l2_dq_buf(mCamFd, &index, V4L2_MEMORY_MMAP);

    if (mTakingPictureEnabled && (cnt < 15)) {
        cnt++;
    } else {
        /* Timestamp the current frame, and notify the camera HAL about new frame. */
        mCurFrameTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        mCameraHAL->onNextFrameAvailable(mBufVirAddr[index], mCurFrameTimestamp, this);
        mDeliveringFrames = true;
        cnt = 0;
    }

    __v4l2_q_buf(mCamFd, mFrameBufferSize, (int)mBufVirAddr[index], index, V4L2_MEMORY_MMAP);
    
    return true;
}

}; /* namespace android */
