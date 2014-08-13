#ifndef ANDROID_SAM_COMMON_H_
#define ANDROID_SAM_COMMON_H_

#include <fcntl.h>
#include <errno.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <utils/StrongPointer.h>

#include "gralloc_priv.h"

#include <utils/Vector.h>
#include "overlayer.h"
#include "heolayer.h"
#include "hwvsync.h"

using namespace android;

typedef struct hwc_context{
    hwc_composer_device_1_t     device;

    /* our private state goes below here */
    const hwc_procs_t         *procs;
    sp<hwvsync>               mVsync;
    bool                      mFirst;
    unsigned int              num_of_fb_layer;
    unsigned int              num_of_hwc_layer;
}hwc_context_t;

#endif /* ANDROID_SAM_COMMON_H_*/
