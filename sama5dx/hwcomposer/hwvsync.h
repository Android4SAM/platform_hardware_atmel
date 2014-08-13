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
 
#ifndef ANDROID_HWVSYNC_H
#define ANDROID_HWVSYNC_H

#include <sys/types.h>
#include <utils/Errors.h>
#include <utils/Thread.h>
#include <poll.h>
#include <linux/ioctl.h>
#include <hardware/hwcomposer.h>

namespace android {

#define ATMEL_LCDFB_SET_VSYNC_INT         _IOW ('F', 206, unsigned int)

class hwvsync: public Thread {
public:

    hwvsync();
    status_t initCheck() const;
    status_t eventControl(int enable);
    status_t setProcs(hwc_procs_t const* procs);
    virtual ~hwvsync();
    virtual bool threadLoop();
    virtual status_t    readyToRun();

    status_t echo();

private:
    status_t        handleVsyncEvent();
    status_t        mInit;
    int32_t         mVsyncFd;
    const hwc_procs_t*    mProcs;
    struct pollfd   mPollFds[1];
};

}

#endif // ANDROID_HWVSYNC_H

