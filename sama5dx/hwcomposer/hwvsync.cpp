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
#include <cutils/log.h>
#include <sys/ioctl.h>

#include "hwvsync.h"

namespace android {

hwvsync::hwvsync()
    : mInit(UNKNOWN_ERROR),
      mVsyncFd(-1)
{
    mVsyncFd = open("/sys/class/graphics/fb0/device/vsync", O_RDONLY);
    if(mVsyncFd < 0) {
        ALOGE("failed to open vsync attribute");
        mInit = UNKNOWN_ERROR;
    } else {
        mPollFds[0].fd = mVsyncFd;
        mPollFds[0].events = POLLPRI;
        mInit = OK;
    }

    ALOGD("hwvsync init successful");
}

hwvsync::~hwvsync()
{
    if(mInit == OK) {
        requestExitAndWait();
    }

    if(mVsyncFd >= 0)
        close(mVsyncFd);

    ALOGD("hwvsync exit");
}

status_t hwvsync::initCheck() const
{
    return mInit;
}

status_t hwvsync::eventControl(int enable)
{
    int32_t lcdfb;
    int val = !!enable;
    char const * const name = "/dev/graphics/fb0";

    lcdfb = open(name, O_RDWR);

    if(lcdfb < 0) {
        ALOGE("Cannot open /dev/graphics/fb0");
        return BAD_VALUE;
    }

    if (ioctl(lcdfb, ATMEL_LCDFB_SET_VSYNC_INT, &val) < 0) {
        ALOGE("ATMEL_LCDFB_SET_VSYNC_INT failed : %s", strerror(errno));
        return BAD_VALUE;
    }

    close(lcdfb);

    return NO_ERROR;
}

status_t hwvsync::setProcs(hwc_procs_t const* procs)
{
    mProcs = procs;
    return NO_ERROR;
}

status_t hwvsync::echo()
{
    ALOGE("hwvsync enter");
    return NO_ERROR;
}

status_t hwvsync::readyToRun()
{
    char temp[4096];
    int32_t err = read(mVsyncFd, temp, sizeof(temp));

    if(err < 0) {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return BAD_VALUE;
    }

    if(!mProcs) {
        ALOGE("mProcs not init");
        return NO_INIT;
    }

    return NO_ERROR;
}

bool hwvsync::threadLoop()
{
    int32_t err = poll(mPollFds, 1, -1);

    if(err == -1) {
        ALOGE("error in vsync thread: %s", strerror(errno));
        if (errno == EINTR)
            return false;
        return true;
    }
    
    if(mPollFds[0].revents & POLLPRI) {
        handleVsyncEvent();
    }

    return true;
}

status_t hwvsync::handleVsyncEvent()
{
    if(!mProcs)
        return NO_INIT;

    int32_t err = lseek(mVsyncFd, 0, SEEK_SET);
    if(err < 0) {
        ALOGE("error seeking to vsync timestamp: %s", strerror(errno));
        return INVALID_OPERATION;
    }

    char buf[4096];
    err = read(mVsyncFd, buf, sizeof(buf));
    if (err < 0) {
        ALOGE("error reading vsync timestamp: %s", strerror(errno));
        return INVALID_OPERATION;
    }
    buf[sizeof(buf) - 1] = '\0';

    errno = 0;
    uint64_t timestamp = strtoull(buf, NULL, 0);
    if (!errno)
        mProcs->vsync(mProcs, 0, timestamp);

    return NO_ERROR;
}

}; // namespace android
