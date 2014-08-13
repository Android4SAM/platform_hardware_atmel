/*
 * Copyright (C) 2010 The Android Open Source Project
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

#include <dirent.h>
#include <sys/resource.h>
#include <cutils/properties.h>
#include <EGL/egl.h>

#include "hwcomposer.h"

Vector < sp<HwcomposerInterface> > mLayers;

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Atmel SAM hwcomposer module",
        author: "ATMEL",
        methods: &hwc_module_methods,
        dso: 0,
        reserved: {0},
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_1_t const* l) {
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

static int hwc_prepare(struct hwc_composer_device_1 *dev, size_t numDisplays,
        hwc_display_contents_1_t** displays) {

    if (!numDisplays || displays == NULL) {
        return 0;
    }
    
    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    
    //if geometry is not changed, there is no need to do any work here
    if (!list || (!(list->flags & HWC_GEOMETRY_CHANGED)))
        return 0;
    
    hwc_context_t* ctx = (hwc_context_t*)dev;
    uint32_t layersize = mLayers.size();

    //We don't want to display the boot animation by overlayer
    if(ctx->mFirst && list->numHwLayers < 2)
        return 0;

    ctx->num_of_hwc_layer = 0;
    ctx->num_of_fb_layer = 0;

    //once numHwLayers > 1, we think boot animation has finished.
    //After that, we should able to handle some cases which has only 1 hwLayers, such as full screen video
    ctx->mFirst = false;

    for (uint32_t i = 0; i < layersize; i++) {
        mLayers[i]->storeDisplayerStatus(false, 0);
    }

    for (int i = list->numHwLayers -1 ; i >= 0 ; i--) {
        hwc_layer_1_t* cur = &list->hwLayers[i];
        bool find = false;
        for (uint32_t j = 0; j < layersize; j++) {
            if(mLayers[j]->checkFormat(cur, i) != NO_ERROR)
                continue;

            if(mLayers[j]->prepare(cur, i) != NO_ERROR) {
                ALOGE("Prepare error for layer(%d)", i);
                continue;
            }
            find = true;
            ctx->num_of_hwc_layer++;
            cur->compositionType = HWC_OVERLAY;
            cur->hints = HWC_HINT_CLEAR_FB;
            break;
        }

        if(!find) {
            ctx->num_of_fb_layer++;
            cur->compositionType = HWC_FRAMEBUFFER;
        }
    }

    for (uint32_t i = 0; i < layersize; i++) {
        uint32_t layerid;
        if(!mLayers[i]->getDisplayer(layerid))
            mLayers[i]->reset();
    }
    return 0;
}

static int hwc_set(struct hwc_composer_device_1 *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || displays == NULL) {
        ALOGD("set: empty display list");
        return 0;
    }

    hwc_display_t dpy = NULL;
    hwc_surface_t sur = NULL;
    hwc_display_contents_1_t* list = displays[0];  // ignore displays beyond the first
    
    if (list != NULL) {
        dpy = list->dpy;
        sur = list->sur;
    }
    
    if (dpy == NULL && sur == NULL && list == NULL) {
        // release our resources, the screen is turning off
        // in our case, there is nothing to do.
        return 0;
    }
    
    hwc_context_t *ctx = (hwc_context_t *)dev;
    uint32_t layersize = mLayers.size();
    hwc_layer_1_t* cur;

    bool need_swap_buffers = ctx->num_of_fb_layer > 0;

    if (!list) {
        /* turn off the all windows */
        for (uint32_t i = 0; i < layersize; i++) {
            mLayers[i]->reset();
        }
        ctx->num_of_hwc_layer = 0;
        return 0;
    }

    /* copy the content of hardware layers here */
    for (uint32_t i = 0; i < layersize; i++) {
        uint32_t layerid;
        if(!mLayers[i]->getDisplayer(layerid))
            continue;

        cur = &list->hwLayers[layerid];
        mLayers[i]->display(cur);
    }

    /* compose the hardware layers here */
    // Base layer
    if (need_swap_buffers || !list) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            ALOGE("%s: eglSwapBuffers failed", __func__);
            return HWC_EGL_ERROR;
        }
    }
 
    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
        hwc_procs_t const* procs)
{
    hwc_context_t *ctx = (hwc_context_t *)dev;
    ctx->procs = procs;
    ctx->mVsync->setProcs(procs);
    ctx->mVsync->run();
}


static int hwc_event_control(struct hwc_composer_device_1* dev,
        int dpy, int event, int enabled)
{
    hwc_context_t *ctx = (hwc_context_t *)dev;
    switch (event) {
    case HWC_EVENT_VSYNC:
        return ctx->mVsync->eventControl(enabled);
    default:
        return -EINVAL;
    }
}

static int hwc_blank(struct hwc_composer_device_1 *dev, int dpy, int blank)
{
    // We're using an older method of screen blanking based on
    // early_suspend in the kernel.  No need to do anything here.
    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    hwc_context_t* ctx = (hwc_context_t*)dev;

    while(mLayers.size() != 0) {
        mLayers.pop();
    }

    if (ctx) {
        ctx->mVsync = NULL;
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/

static status_t get_num_of_ovl_layers(uint32_t* numbers)
{
    DIR*    dir;
    struct dirent* de;
    char value[PROPERTY_VALUE_MAX];
    
    *numbers = 0;
    
    if (!(dir = opendir("/dev/graphics"))) {
        ALOGE("opendir failed (%s)", strerror(errno));
        return NO_INIT;
    }

    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, "fb0"))
            continue;
        (*numbers)++;
    }
    closedir(dir);

    if (property_get("ro.hwc.ovl_num", value, "2") > 0) {
        *numbers = _MIN(atoi(value), *numbers);
    }

    return NO_ERROR;
}

static status_t get_num_of_heo_layers(uint32_t* numbers)
{
    DIR*    dir;
    struct dirent* de;
    char value[PROPERTY_VALUE_MAX];
    
    *numbers = 0;
    
    if (!(dir = opendir("/sys/class/video4linux"))) {
        ALOGE("opendir failed (%s)", strerror(errno));
        return NO_INIT;
    }

    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, "fb0"))
            continue;
        (*numbers)++;
    }
    closedir(dir);

    if (property_get("ro.hwc.ovl_heo_num", value, "1") > 0) {
        *numbers = _MIN(atoi(value), *numbers);
    }

    return NO_ERROR;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    hwc_context_t *dev;
    sp<hwvsync> vsync;
    uint32_t num_of_avail_ovl, num_of_avail_heo;
    
    if (strcmp(name, HWC_HARDWARE_COMPOSER))
        return -EINVAL;

    get_num_of_ovl_layers(&num_of_avail_ovl);
    get_num_of_heo_layers(&num_of_avail_heo);

    for (unsigned int i = 0; i < num_of_avail_ovl; i++) {
        sp<HwcomposerInterface> layer = new overlayer(num_of_avail_ovl - i);
        if(layer->initCheck() != OK)
            ALOGE("fb%d initcheck error", i);
        else
            mLayers.add(layer);
    }

    for (unsigned int i = 0; i < num_of_avail_heo; i++) {
        sp<HwcomposerInterface> layer = new heolayer(i);
        if(layer->initCheck() != OK)
            ALOGE("video%d initcheck error", i);
        else
            mLayers.add(layer);
    }

    //There is no hardware layers, so return here.
    if(mLayers.size() == 0)
        goto no_layer;

    dev = (hwc_context_t*)malloc(sizeof(*dev));
    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    vsync = new hwvsync();

    if(vsync->initCheck() != OK)
        ALOGE("vsync init error");
    else
        dev->mVsync = vsync;

    dev->mFirst = true;
    
    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;

    dev->device.prepare = hwc_prepare;
    dev->device.set = hwc_set;
    dev->device.eventControl = hwc_event_control;
    dev->device.blank = hwc_blank;
    dev->device.registerProcs = hwc_registerProcs;
    *device = &dev->device.common;
    
    ALOGD("%s:: success\n", __func__);
    return 0;

no_layer:
    return -EINVAL;
}
