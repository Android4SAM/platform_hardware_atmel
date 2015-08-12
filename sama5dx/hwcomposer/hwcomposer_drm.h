#ifndef ANDROID_HWC_H_
#define ANDROID_HWC_H_
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>

#include <cutils/compiler.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <utils/Mutex.h>

#include <EGL/egl.h>
#include <sync/sync.h>
#include "sw_sync.h"

#include "gralloc_priv.h"
#include "drm_fourcc.h"
#include "xf86drm.h"
#include "xf86drmMode.h"

#define DEBUG_ST_HWCOMPOSER 0
#define DEBUG_ST_HWCOMPOSER_FENCE 0

#define HWC_DEFAULT_CONFIG 0
#define to_ctx(dev) ((hwc_context_t *)dev)

#ifndef MIN
#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#endif

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

/* FENCE_CURRENT_BUF:
 *  Number of ticks before signaling a fence for a buffer ON DISPLAY:
 *  After one tick the buffer is replaced by another buffer : it is no more on
 *  display and has to be signaled.
 */
#define FENCE_CURRENT_BUF 1

/* FENCE_NEW_BUF:
 *  Number of ticks before signaling a fence for a NEW buffer:
 *  After one tick the buffer gets on display. After a second tick the buffer is
 *  replaced by another buffer : it is no more on display and has to be signaled.
 */
#define FENCE_NEW_BUF 2

#define MAX_DRM_PLANES 10
#define COMPO_INDEX    -1       /* virtual plane index for compo */
#define CURSOR_INDEX   -2       /* virtual plane index for cursor */
#define HWC_CURSOR_OVERLAY 4

typedef struct fb_info
{
    int share_fd;               /* dmabuf fd */
    uint32_t drm_fb_id;         /* DRM framebuffer id */
    uint32_t bo_handle;         /* KMS buffer object handle */
    bool updated;               /* True if the fb information has been updated */
} fb_info_t;

typedef struct fb_status
{
    fb_info_t current;
    fb_info_t next;
} fb_status_t;

typedef struct cursor_info
{
    int share_fd;               /* dmabuf fd */
    uint32_t bo_handle;         /* KMS buffer object handle */
    bool updated;               /* True if the cursor information has been updated */
} cursor_info_t;

typedef struct cursor_status
{
    cursor_info_t current;
    cursor_info_t next;
    bool support;
} cursor_status_t;

#if DEBUG_ST_HWCOMPOSER_FENCE
enum
{
    PT_FREE,
    PT_PENDING
};

typedef struct timeline_dbg
{
    int status;
    unsigned value;
} timeline_dbg_t;

#define DBG_MAX_PT 10
#endif /* DEBUG_ST_HWCOMPOSER_FENCE */

typedef struct timeline_info
{
    int timeline;
    pthread_mutex_t lock;       /* protect signaled_fences */
    unsigned signaled_fences;
#if DEBUG_ST_HWCOMPOSER_FENCE
    timeline_dbg_t dbg_status[DBG_MAX_PT];
#endif                          /* DEBUG_ST_HWCOMPOSER_FENCE */
} timeline_info_t;

typedef struct kms_display
{
    drmModeConnectorPtr con;
    drmModeEncoderPtr enc;
    drmModeCrtcPtr crtc;
    int crtc_id;
    drmModeModeInfoPtr mode;
    drmEventContext evctx;
    int vsync_on;
    struct hwc_context *ctx;

    /* sync */
    timeline_info_t retire_sync;
    timeline_info_t release_sync[MAX_DRM_PLANES];
    timeline_info_t release_sync_cursor;

    /* fb and cursor info */
    fb_status_t fb_main;
    fb_status_t fb_plane[MAX_DRM_PLANES];
    cursor_status_t cursor;

    bool compo_updated;

    android::Mutex compo_lock;    /* protect compo info of this struct */
} kms_display_t;

typedef struct hwc_context
{
    hwc_composer_device_1_t device;
    pthread_mutex_t ctx_mutex;
    const hwc_procs_t *cb_procs;

    const struct gralloc_module_t *gralloc;

    int drm_fd;
    kms_display_t displays[HWC_NUM_DISPLAY_TYPES];

    pthread_t event_thread;

    int32_t xres;
    int32_t yres;
    int32_t xdpi;
    int32_t ydpi;
    int32_t vsync_period;

    /* drm planes management */
    uint64_t used_planes;
    uint32_t plane_id[MAX_DRM_PLANES];
    unsigned int nb_planes;
} hwc_context_t;

#endif //#ifndef ANDROID_HWC_H_
