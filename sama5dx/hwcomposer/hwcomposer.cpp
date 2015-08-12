#include <hwcomposer_drm.h>
#include "gralloc_priv.h"

/*
 * The primary and external displays are controlled by the ro.disp.conn.primary
 * and ro.disp.conn.external properties.
 * This properties define the connector of the displays, their values are strings
 * as defined by the DRM interface (DRM_MODE_CONECTOR_xyz).
 *
 * Configuration example (HDMI for main, LVDS for external):
 *  setprop ro.disp.conn.primary HDMIA
 *  setprop ro.disp.conn.external LVDS
 *
 * If a property is set to "Unknown", then the first (resp. second) connector
 * as listed by the display driver is used for the primary (resp. external)
 * display.
 *
 * If ro.disp.conn.primary is not defined, then it is assumed to be set to
 * "Unknown".
 *
 * If ro.disp.conn.external is set to "OFF" or is not defined, then the external
 * display is not enabled
 */

struct hwc_fourcc
{
    int hwc_format;
    unsigned int fourcc;
};

static const struct hwc_fourcc to_fourcc[] = {
    {HAL_PIXEL_FORMAT_RGBA_8888, DRM_FORMAT_ARGB8888},
    {HAL_PIXEL_FORMAT_RGBX_8888, DRM_FORMAT_XRGB8888},
    {HAL_PIXEL_FORMAT_BGRA_8888, DRM_FORMAT_ARGB8888},
    {HAL_PIXEL_FORMAT_RGB_888, DRM_FORMAT_RGB888},
    {HAL_PIXEL_FORMAT_RGB_565, DRM_FORMAT_RGB565},
    {HAL_PIXEL_FORMAT_YV12, DRM_FORMAT_NV12},
};


static unsigned int
hnd_to_fourcc(private_handle_t const *hnd)
{
    for (unsigned int i = 0; i < ARRAY_SIZE(to_fourcc); i++)
        if (to_fourcc[i].hwc_format == hnd->format)
            return to_fourcc[i].fourcc;

    ALOGI("hnd_to_fourcc can't find matching format for %ul", hnd->format);
    return 0;
}

#define CONN_STR_AND_INT(type) { DRM_MODE_CONNECTOR_ ## type, #type }

struct hwc_connector
{
    int type;
    char name[64];
};

static const struct hwc_connector connector_list[] = {
    CONN_STR_AND_INT(Unknown),
    CONN_STR_AND_INT(VGA),
    CONN_STR_AND_INT(DVII),
    CONN_STR_AND_INT(DVID),
    CONN_STR_AND_INT(DVIA),
    CONN_STR_AND_INT(Composite),
    CONN_STR_AND_INT(SVIDEO),
    CONN_STR_AND_INT(LVDS),
    CONN_STR_AND_INT(Component),
    CONN_STR_AND_INT(9PinDIN),
    CONN_STR_AND_INT(DisplayPort),
    CONN_STR_AND_INT(HDMIA),
    CONN_STR_AND_INT(HDMIB),
    CONN_STR_AND_INT(TV),
    CONN_STR_AND_INT(eDP)
};

#if DEBUG_ST_HWCOMPOSER_FENCE
static void
dbg_timeline_dump(timeline_info_t * timeline, int plane_index)
{
    int i;
    char dump_str[255], val[32];

    switch (plane_index) {
        case CURSOR_INDEX:
            sprintf(dump_str, "   { cursor = [");
            break;

        case COMPO_INDEX:
            sprintf(dump_str, "   { compo  = [");
            break;

        default:
            if (plane_index >= 0)
                sprintf(dump_str, "   { plane%d = [", plane_index);
            else
                sprintf(dump_str, "   { ???  %d = [", plane_index);
            break;
    }

    for (i = 0; i < DBG_MAX_PT; i++) {
        if (timeline->dbg_status[i].status == PT_PENDING) {
            sprintf(val, " %d", timeline->dbg_status[i].value);
            strcat(dump_str, val);
        }
    }

    strcat(dump_str, "] }");

    ALOGI(dump_str);
}

static void
dbg_timeline_insert(timeline_info_t * timeline, unsigned value)
{
    int i;

    for (i = 0; i < DBG_MAX_PT; i++) {
        if ((timeline->dbg_status[i].status == PT_PENDING) &&
                (timeline->dbg_status[i].value == value))
            return;
    }

    for (i = 0; i < DBG_MAX_PT; i++) {
        if (timeline->dbg_status[i].status == PT_FREE)
            break;
    }

    if (i == DBG_MAX_PT) {
        ALOGE("   { Timeline full! }");
        return;
    }

    timeline->dbg_status[i].status = PT_PENDING;
    timeline->dbg_status[i].value = value;
}

static void
dbg_timeline_remove(timeline_info_t * timeline, unsigned value)
{
    int i;

    for (i = 0; i < DBG_MAX_PT; i++) {
        if ((timeline->dbg_status[i].value == value) &&
                (timeline->dbg_status[i].status == PT_PENDING))
            break;
    }

    if (i == DBG_MAX_PT) {
        ALOGE("   { Timeline : cannot find %d }", value);
        return;
    }

    timeline->dbg_status[i].status = PT_FREE;
}
#endif

static void
release_drm_fb(int fd, fb_info_t * fb_info)
{
    struct drm_mode_destroy_dumb destroy_arg;

    if (fb_info->drm_fb_id)
        drmModeRmFB(fd, fb_info->drm_fb_id);

    if (fb_info->bo_handle) {
        memset(&destroy_arg, 0, sizeof(destroy_arg));
        destroy_arg.handle = fb_info->bo_handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    }
}

static void
release_drm_cursor(int fd, cursor_info_t * cursor_info)
{
    struct drm_mode_destroy_dumb destroy_arg;

    if (cursor_info->bo_handle) {
        memset(&destroy_arg, 0, sizeof(destroy_arg));
        destroy_arg.handle = cursor_info->bo_handle;
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
    }
}

static int
send_vblank_request(hwc_context_t * ctx, int disp)
{
    int ret;

    drmVBlank vbl;

    if (disp == HWC_DISPLAY_PRIMARY)
        vbl.request.type = (drmVBlankSeqType) (DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT);
    else
        vbl.request.type =
                (drmVBlankSeqType) (DRM_VBLANK_RELATIVE | DRM_VBLANK_EVENT | DRM_VBLANK_SECONDARY);

    vbl.request.sequence = 1;
    vbl.request.signal = (unsigned long) &ctx->displays[disp];

    ret = drmWaitVBlank(ctx->drm_fd, &vbl);
    if (ret < 0)
        ALOGE("Failed to request vblank %d", errno);

    return ret;
}

static void
signal_fence(timeline_info_t * timeline, int plane_index)
{
    pthread_mutex_lock(&timeline->lock);

    sw_sync_timeline_inc(timeline->timeline, 1);
    timeline->signaled_fences++;

#if DEBUG_ST_HWCOMPOSER_FENCE
    switch (plane_index) {
        case CURSOR_INDEX:
            ALOGI("   { pt! : cursor @ %d }", timeline->signaled_fences);
            break;

        case COMPO_INDEX:
            ALOGI("   { pt! : compo  @ %d }", timeline->signaled_fences);
            break;

        default:
            if (plane_index >= 0)
                ALOGI("   { pt! : plane%d @ %d }", plane_index, timeline->signaled_fences);
            else
                ALOGI("   { pt! : ???  %d @ %d }", plane_index, timeline->signaled_fences);
            break;
    }

    dbg_timeline_remove(timeline, timeline->signaled_fences);
#else
    (void) plane_index;
#endif

    pthread_mutex_unlock(&timeline->lock);
}

static int
create_fence(timeline_info_t * timeline, unsigned relative, int plane_index)
{
    int fd;
    unsigned new_pt;

    pthread_mutex_lock(&timeline->lock);

    new_pt = timeline->signaled_fences + relative;
    fd = sw_sync_fence_create(timeline->timeline, "Fence", new_pt);

#if DEBUG_ST_HWCOMPOSER_FENCE
    switch (plane_index) {
        case CURSOR_INDEX:
            ALOGI("   { pt+ : cursor @ %d }", new_pt);
            break;

        case COMPO_INDEX:
            ALOGI("   { pt+ : compo  @ %d }", new_pt);
            break;

        default:
            if (plane_index >= 0)
                ALOGI("   { pt+ : plane%d @ %d }", plane_index, new_pt);
            else
                ALOGI("   { pt+ : ???  %d @ %d }", plane_index, new_pt);
            break;
    }

    dbg_timeline_insert(timeline, new_pt);
#else
    (void) plane_index;
#endif

    pthread_mutex_unlock(&timeline->lock);

    return fd;
}

static void
vblank_handler(int fd, unsigned int frame, unsigned int sec, unsigned int usec, void *data)
{
    (void) fd, frame;
    kms_display_t *kdisp = (kms_display_t *) data;
    android::Mutex::Autolock lock(kdisp->compo_lock);
    const hwc_procs_t *procs = kdisp->ctx->cb_procs;
    int i;
    fb_status_t *fb_status;
    cursor_status_t *cursor_status;
    int disp =
            &kdisp->ctx->displays[HWC_DISPLAY_PRIMARY] ==
            kdisp ? HWC_DISPLAY_PRIMARY : HWC_DISPLAY_EXTERNAL;

    ALOGI_IF(DEBUG_ST_HWCOMPOSER, "VSYNC");

    /* Restart the vblank request  */
    send_vblank_request(kdisp->ctx, disp);

    /* Call the vsync callback if requested */
    if (kdisp->vsync_on)
        procs->vsync(procs, disp, sec * (int64_t) 1000000000 + usec * (int64_t) 1000);

    /* For each overlay: signal the release_fences and free resources */
    for (i = 0; i < MAX_DRM_PLANES; i++) {
        fb_status = &kdisp->fb_plane[i];
        if (fb_status->next.updated) {
            /* The buffer actually changed: increment timeline */
            signal_fence(&kdisp->release_sync[i], i);

            release_drm_fb(kdisp->ctx->drm_fd, &fb_status->current);

            fb_status->next.updated = false;
            fb_status->current = fb_status->next;
        }
    }

    /* For main FB : free resources */
    fb_status = &kdisp->fb_main;
    if (fb_status->next.updated) {
        release_drm_fb(kdisp->ctx->drm_fd, &fb_status->current);

        fb_status->next.updated = false;
        fb_status->current = fb_status->next;
    }

    /* For cursor: signal the release_fences and free resources */
    cursor_status = &kdisp->cursor;
    if (cursor_status->next.updated) {
        /* The buffer actually changed: increment timeline */
        signal_fence(&kdisp->release_sync_cursor, CURSOR_INDEX);

        release_drm_cursor(kdisp->ctx->drm_fd, &cursor_status->current);

        cursor_status->next.updated = false;
        cursor_status->current = cursor_status->next;
    }

    /* Signal retireFence */
    if (kdisp->compo_updated) {
        signal_fence(&kdisp->retire_sync, COMPO_INDEX);
        kdisp->compo_updated = false;
    }
#if DEBUG_ST_HWCOMPOSER_FENCE
    dbg_timeline_dump(&kdisp->retire_sync, COMPO_INDEX);
    for (i = 0; i < (int) kdisp->ctx->nb_planes; i++)
        dbg_timeline_dump(&kdisp->release_sync[i], i);
    dbg_timeline_dump(&kdisp->release_sync_cursor, CURSOR_INDEX);
#endif
}

static int
init_display(hwc_context_t * ctx, int disp, uint32_t connector_type)
{
    kms_display_t *d = &ctx->displays[disp];
    const char *modules[] = {
        "i915", "radeon", "nouveau", "vmwgfx", "omapdrm", "exynos",
        "tilcdc", "msm", "sti", "hisi", "atmel-hlcdc"
    };
    int drm_fd, i;
    drmModeResPtr resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder;
    drmModeModeInfoPtr mode;
    uint32_t possible_crtcs;
    private_module_t *m = NULL;

    /* open drm only once for all the displays */
    if (ctx->drm_fd < 0) {
        /* Open DRM device */
        for (unsigned int i = 0; i < ARRAY_SIZE(modules); i++) {
            drm_fd = drmOpen(modules[i], NULL);
            if (drm_fd >= 0) {
                ALOGI("Open %s drm device (%d)", modules[i], drm_fd);
                break;
            }
        }
        if (drm_fd < 0) {
            ALOGE("Failed to open DRM: %s", strerror(errno));
            return -EINVAL;
        }

        ctx->drm_fd = drm_fd;
    } else {
        drm_fd = ctx->drm_fd;
    }

    resources = drmModeGetResources(drm_fd);
    if (!resources) {
        ALOGE("Failed to get resources: %s", strerror(errno));
        goto close;
    }

    if (connector_type == DRM_MODE_CONNECTOR_Unknown) {
        if (disp < resources->count_connectors)
            connector = drmModeGetConnector(drm_fd, resources->connectors[disp]);
    } else {
        for (i = 0; i < resources->count_connectors; i++) {
            connector = drmModeGetConnector(drm_fd, resources->connectors[i]);
            if (connector->connector_type == connector_type)
                break;
            drmModeFreeConnector(connector);
            connector = NULL;
        }
    }

    if (!connector) {
        ALOGE("No connector %d (display %d)", connector_type, disp);
        goto free_ressources;
    }

    mode = &connector->modes[0];
    ALOGI("Display %d: %dx%d, type=%s", disp, mode->hdisplay, mode->vdisplay,
            connector_list[connector->connector_type].name);

    encoder = drmModeGetEncoder(drm_fd, connector->encoders[0]);
    if (!encoder) {
        ALOGE("Failed to get encoder");
        goto free_connector;
    }

    if (!(encoder->possible_crtcs & (1 << disp)))
        goto free_encoder;

    d->con = connector;
    d->enc = encoder;
    d->crtc_id = resources->crtcs[disp];
    d->mode = mode;
    d->evctx.version = DRM_EVENT_CONTEXT_VERSION;
    d->evctx.vblank_handler = vblank_handler;
    d->ctx = ctx;

    drmModeFreeResources(resources);

    /* sync init */
    d->retire_sync.timeline = sw_sync_timeline_create();
    d->retire_sync.signaled_fences = 0;
    pthread_mutex_init(&d->retire_sync.lock, (const pthread_mutexattr_t *) NULL);

    for (i = 0; i < MAX_DRM_PLANES; i++) {
        d->release_sync[i].timeline = sw_sync_timeline_create();
        d->release_sync[i].signaled_fences = 0;
        pthread_mutex_init(&d->release_sync[i].lock, (const pthread_mutexattr_t *) NULL);
    }

    if (!drmModeSetCursor(drm_fd, d->crtc_id, 0, 0, 0)) {
        d->cursor.support = true;
        d->release_sync_cursor.timeline = sw_sync_timeline_create();
        d->release_sync_cursor.signaled_fences = 0;
        pthread_mutex_init(&d->release_sync_cursor.lock, (const pthread_mutexattr_t *) NULL);
    }

    d->vsync_on = 0;

    return 0;

  free_encoder:
    drmModeFreeEncoder(encoder);
  free_connector:
    drmModeFreeConnector(connector);
  free_ressources:
    drmModeFreeResources(resources);
  close:
    return -1;
}

static void
destroy_display(kms_display_t * d)
{
    int i;

    if (d->crtc)
        drmModeFreeCrtc(d->crtc);
    if (d->enc)
        drmModeFreeEncoder(d->enc);
    if (d->con)
        drmModeFreeConnector(d->con);
    memset(d, 0, sizeof(*d));

    for (i = 0; i < MAX_DRM_PLANES; i++)
        close(d->release_sync[i].timeline);

    close(d->retire_sync.timeline);

    if (d->cursor.support)
        close(d->release_sync_cursor.timeline);
}

#if DEBUG_ST_HWCOMPOSER
static const char *
composition_type_str(int32_t type)
{
    const char *name[] = {
        "FB      ",
        "Overlay ",
        "Backgnd ",
        "FBTarget",
        "Sideband",
        "Cursor  ",
        "UNKNOWN "
    };

    if ((type >= HWC_FRAMEBUFFER) && (type <= HWC_CURSOR_OVERLAY))
        return name[type];

    return name[HWC_CURSOR_OVERLAY + 1];
}

static void
dump_layer(hwc_layer_1_t * l, int i)
{
    private_handle_t const *hnd = reinterpret_cast < private_handle_t const *>(l->handle);

    ALOGI(" [%d] %s, flags=0x%02x, handle=%p, fd=%3d, tr=0x%02x, blend=0x%04x,"
            " {%dx%d @ (%d,%d)} <- {%dx%d @ (%d,%d)}, acqFd=%d",
            i, composition_type_str(l->compositionType), l->flags,
            l->handle, hnd ? hnd->share_fd : -1,
            l->transform, l->blending,
            l->displayFrame.right - l->displayFrame.left,
            l->displayFrame.bottom - l->displayFrame.top, l->displayFrame.left, l->displayFrame.top,
            l->sourceCrop.right - l->sourceCrop.left, l->sourceCrop.bottom - l->sourceCrop.top,
            l->sourceCrop.left, l->sourceCrop.top, l->acquireFenceFd);
}
#else
static void
dump_layer(hwc_layer_1_t * l, int i)
{
    (void) l, i;
}
#endif

static void *
event_handler(void *arg)
{
    hwc_context_t *ctx = (hwc_context_t *) arg;
    pthread_mutex_t *mutex = &ctx->ctx_mutex;
    int drm_fd = ctx->drm_fd;
    drmEventContext evctx = {
        .version = DRM_EVENT_CONTEXT_VERSION,
        .vblank_handler = vblank_handler,
        .page_flip_handler = NULL,
    };
    struct pollfd pfds[1] = { {
                    .fd = drm_fd,
                    .events = POLLIN,
            .revents = POLLERR}
    };

    // From documentation for hwc_procs, the vsync event must be handled
    // on a thread with priority HAL_PRIORITY_URGENT_DISPLAY or higher.
    // This is further explained in graphics.h.
    setpriority(PRIO_PROCESS, 0, HAL_PRIORITY_URGENT_DISPLAY);

    /* start asking for VBLANK */
    send_vblank_request(ctx, HWC_DISPLAY_PRIMARY);
    send_vblank_request(ctx, HWC_DISPLAY_EXTERNAL);

    while (1) {
        int ret = poll(pfds, ARRAY_SIZE(pfds), 60000);
        if (ret < 0) {
            ALOGE("Event handler error %d", errno);
            break;
        } else if (ret == 0) {
            ALOGI("Event handler timeout");
            continue;
        }
        for (int i = 0; i < ret; i++) {
            if (pfds[i].fd == drm_fd)
                drmHandleEvent(drm_fd, &evctx);
        }
    }
    return NULL;
}

static bool
is_display_connected(hwc_context_t * ctx, int disp)
{
    if ((disp != HWC_DISPLAY_PRIMARY) && (disp != HWC_DISPLAY_EXTERNAL))
        return false;

    if (!ctx->displays[disp].con)
        return false;

    if (ctx->displays[disp].con->connection == DRM_MODE_CONNECTED)
        return true;

    return false;
}

static bool
set_zorder(hwc_context_t * ctx, int plane_id, int zorder)
{
    drmModeObjectPropertiesPtr properties = NULL;
    drmModePropertyPtr property = NULL;
    int i, ret;

    properties = drmModeObjectGetProperties(ctx->drm_fd, plane_id, DRM_MODE_OBJECT_PLANE);

    if (!properties)
        return false;

    for (i = 0; i < (int) properties->count_props; ++i) {
        property = drmModeGetProperty(ctx->drm_fd, properties->props[i]);
        if (!property)
            continue;
        if (strcmp(property->name, "zpos") == 0)
            break;
        drmModeFreeProperty(property);
    }

    if (i == (int) properties->count_props)
        goto free_properties;

    ret = drmModeObjectSetProperty(ctx->drm_fd, plane_id,
            DRM_MODE_OBJECT_PLANE, property->prop_id, zorder);
    drmModeFreeProperty(property);

  free_properties:
    drmModeFreeObjectProperties(properties);
    return ! !ret;
}

static int
get_plane_index(hwc_context_t * ctx, uint32_t drm_plane_id)
{
    unsigned int i;

    if (!drm_plane_id) {
        ALOGE("Invalid plane_id (%d)", drm_plane_id);
        return -EINVAL;
    }

    for (i = 0; i < MAX_DRM_PLANES; i++) {
        if (ctx->plane_id[i] == drm_plane_id)
            return i;
    }

    ALOGE("Unknown plane_id (%d)", drm_plane_id);
    return -EINVAL;
}

static int
create_drm_fb(hwc_context_t * ctx, private_handle_t const *hnd, uint32_t * fb, uint32_t bo[4])
{
    int ret;
    unsigned int fourcc;
    uint32_t width, height, pitch[4], offset[4] = { 0 };

    ret = drmPrimeFDToHandle(ctx->drm_fd, hnd->share_fd, bo);
    if (ret) {
        ALOGE("Failed to get fd for DUMB buffer %s", strerror(errno));
        return ret;
    }

    fourcc = hnd_to_fourcc(hnd);
    if (!fourcc) {
        ALOGE("unknown pixel format (%d)", hnd->format);
        return -EINVAL;
    }

    width = hnd->width;
    height = hnd->height;

    if (fourcc == DRM_FORMAT_NV12) {
        bo[1] = bo[0];
        pitch[0] = width;
        pitch[1] = width * 2;
        offset[1] = width * height;
    } else {
        int bpp;
               
               switch (fourcc) {
                       case DRM_FORMAT_RGB888:
                               bpp = 3;
                               break;
                       case DRM_FORMAT_RGB565:
                               bpp = 2;
                               break;
                       case DRM_FORMAT_ABGR8888:
                       case DRM_FORMAT_XBGR8888:
                       case DRM_FORMAT_ARGB8888:
                       default:
                               bpp = 4;
                       break;
               }
               pitch[0] = width * bpp;   //stride
    }

    ret = drmModeAddFB2(ctx->drm_fd, width, height, fourcc, bo, pitch, offset, fb, 0);
    if (ret) {
        ALOGE("cannot create framebuffer (%d): %s", errno, strerror(errno));
        return ret;
    }

    ALOGI_IF(DEBUG_ST_HWCOMPOSER, "  Created DRM fb (dmafd=%d)", hnd->share_fd);

    return 0;
}

static void
set_fb_info(fb_info_t * fb_info, uint32_t fb_id, uint32_t bo, int fd)
{
    fb_info->drm_fb_id = fb_id;
    fb_info->bo_handle = bo;
    fb_info->share_fd = fd;
    fb_info->updated = true;
}

static void
set_cursor_info(cursor_info_t * cursor_info, uint32_t bo, int fd)
{
    cursor_info->bo_handle = bo;
    cursor_info->share_fd = fd;
    cursor_info->updated = true;
}

static int
update_display_locked(hwc_context_t * ctx, int disp, hwc_display_contents_1_t * display)
{
    int ret, zorder = 2, drm_plane_id, plane_index;
    uint32_t fb, bo[4] = { 0 };
    uint64_t used_planes = 0;
    bool used_cursor = false;
    hwc_layer_1_t *target;
    fb_status_t *fb_status;
    cursor_status_t *cursor_status;
    bool is_fb_updated, is_cursor_updated;
    kms_display_t *kdisp = &ctx->displays[disp];
    android::Mutex::Autolock lock(kdisp->compo_lock);

    if (!is_display_connected(ctx, disp))
        return 0;

    ALOGI_IF(DEBUG_ST_HWCOMPOSER, "UPDATE (%d layers)", display->numHwLayers);

    for (size_t i = 0; i < display->numHwLayers; i++) {
        target = &display->hwLayers[i];

        if (!target)
            continue;

        dump_layer(target, (int) i);

        private_handle_t const *hnd = reinterpret_cast < private_handle_t const *>(target->handle);

        if (!hnd)
            continue;

        if ((target->compositionType != HWC_FRAMEBUFFER_TARGET) &&
                (target->compositionType != HWC_OVERLAY) &&
                (target->compositionType != HWC_CURSOR_OVERLAY))
            continue;

        /* wait for sync */
        if (target->acquireFenceFd >= 0) {
            int ret = sync_wait(target->acquireFenceFd, 1000);
            if (ret < 0) {
                ALOGE("%s: sync_wait error!! error no = %d err str = %s",
                        __FUNCTION__, errno, strerror(errno));
            }
            close(target->acquireFenceFd);
            target->acquireFenceFd = -1;
        }

        if (!(hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION)) {
            ALOGE("private_handle_t isn't using ION, hnd->flags %d", hnd->flags);
            return -EINVAL;
        }

        /* Update display */
        switch (target->compositionType) {
            case HWC_FRAMEBUFFER_TARGET:
                /* Get resources */
                fb_status = &kdisp->fb_main;

                is_fb_updated = (fb_status->current.share_fd != hnd->share_fd);

                if (is_fb_updated) {
                    ret = create_drm_fb(ctx, hnd, &fb, bo);
                    if (ret)
                        return ret;
                } else {
                    fb = fb_status->current.drm_fb_id;
                }

                /* Update display */
                ret = drmModeSetCrtc(ctx->drm_fd, kdisp->crtc_id, fb, 0, 0,
                        &kdisp->con->connector_id, 1, kdisp->mode);

                if (ret) {
                    ALOGE("  drmModeSetCrtc failed (%d)", errno);
                    return ret;
                }

                ALOGI_IF(DEBUG_ST_HWCOMPOSER, "  Called drmModeSetCrtc (%s buffer)",
                        is_fb_updated ? "updated" : "unchanged");

                /* Update status */
                if (is_fb_updated)
                    set_fb_info(&fb_status->next, fb, bo[0], hnd->share_fd);

                break;

            case HWC_OVERLAY:
                /* Get resources */
                drm_plane_id = hnd->plane_id;
                plane_index = get_plane_index(ctx, drm_plane_id);
                fb_status = &kdisp->fb_plane[plane_index];

                is_fb_updated = (fb_status->current.share_fd != hnd->share_fd);

                if (is_fb_updated) {
                    ret = create_drm_fb(ctx, hnd, &fb, bo);
                    if (ret)
                        return ret;
                } else {
                    fb = fb_status->current.drm_fb_id;
                }

                /* Update display */
                set_zorder(ctx, drm_plane_id, zorder++);

                ret = drmModeSetPlane(ctx->drm_fd, drm_plane_id, kdisp->crtc_id, fb, 0,
                        target->displayFrame.left, target->displayFrame.top,
                        target->displayFrame.right - target->displayFrame.left,
                        target->displayFrame.bottom - target->displayFrame.top,
                        target->sourceCrop.left << 16, target->sourceCrop.top << 16,
                        (target->sourceCrop.right - target->sourceCrop.left) << 16,
                        (target->sourceCrop.bottom - target->sourceCrop.top) << 16);

                if (ret) {
                    ALOGE("  drmModeSetPlane failed (%d)", errno);
                    return ret;
                }

                used_planes |= 1 << plane_index;

                ALOGI_IF(DEBUG_ST_HWCOMPOSER, "  Called drmModeSetPlane (%s buffer)",
                        is_fb_updated ? "updated" : "unchanged");

                /* Set fence to be signaled when the buffer gets out of the screen */
                if (target->releaseFenceFd == -1)
                    target->releaseFenceFd = create_fence(&kdisp->release_sync[plane_index],
                            is_fb_updated ? FENCE_NEW_BUF : FENCE_CURRENT_BUF, plane_index);

                /* Update status */
                if (is_fb_updated)
                    set_fb_info(&fb_status->next, fb, bo[0], hnd->share_fd);

                break;

            case HWC_CURSOR_OVERLAY:
                /* Get resources */
                cursor_status = &kdisp->cursor;

                is_cursor_updated = (cursor_status->current.share_fd != hnd->share_fd);
                if (is_cursor_updated) {
                    ret = drmPrimeFDToHandle(ctx->drm_fd, hnd->share_fd, bo);
                    if (ret) {
                        ALOGE("Failed to get handle for cursor %s", strerror(errno));
                        return ret;
                    }

                    ret = drmModeSetCursor(ctx->drm_fd, kdisp->crtc_id, bo[0], hnd->width,
                            hnd->height);
                    if (ret) {
                        ALOGE("Failed to set cursor %s", strerror(errno));
                        return ret;
                    }
                }

                /* Update display */
                ret = drmModeMoveCursor(ctx->drm_fd, kdisp->crtc_id,
                        target->displayFrame.left, target->displayFrame.top);
                if (ret) {
                    ALOGE("Failed to move cursor %s", strerror(errno));
                    return ret;
                }

                ALOGI_IF(DEBUG_ST_HWCOMPOSER, "  Called drmModeMoveCursor (%s buffer)",
                        is_cursor_updated ? "updated" : "unchanged");

                used_cursor = true;

                /* set fence */
                if (target->releaseFenceFd == -1)
                    target->releaseFenceFd = create_fence(&kdisp->release_sync_cursor,
                            is_cursor_updated ? FENCE_NEW_BUF : FENCE_CURRENT_BUF, CURSOR_INDEX);

                /* Update status */
                if (is_cursor_updated)
                    set_cursor_info(&cursor_status->next, bo[0], hnd->share_fd);

                break;

            default:
                break;
        }
    }

    /* Look for freshly disabled planes */
    for (unsigned int i = 0; i < ctx->nb_planes; i++) {
        fb_status = &kdisp->fb_plane[i];
        if (!(used_planes & 1) && fb_status->current.drm_fb_id) {
            ALOGI_IF(DEBUG_ST_HWCOMPOSER, "Disabling plane %d", ctx->plane_id[i]);

            ret = drmModeSetPlane(ctx->drm_fd, ctx->plane_id[i], kdisp->crtc_id,
                    0, 0, 0, 0, 0, 0, 0, 0, 0, 0);

            if (ret) {
                ALOGE("drmModeSetPlane disabling failed (%d)", errno);
                return ret;
            }

            /* Update status */
            set_fb_info(&fb_status->next, 0, 0, 0);
        }
        used_planes >>= 1;
    }

    /* Look for freshly disabled cursor */
    cursor_status = &kdisp->cursor;
    if (!used_cursor && cursor_status->current.bo_handle) {
        ALOGI_IF(DEBUG_ST_HWCOMPOSER, "Disabling cursor");

        ret = drmModeSetCursor(ctx->drm_fd, kdisp->crtc_id, 0, 0, 0);

        if (ret) {
            ALOGE("drmModeSetPlane disabling failed (%d)", errno);
            return ret;
        }

        /* Update status */
        set_cursor_info(&cursor_status->next, 0, 0);
    }

    if (display->retireFenceFd == -1)
        display->retireFenceFd = create_fence(&kdisp->retire_sync, FENCE_NEW_BUF, COMPO_INDEX);

    kdisp->compo_updated = true;

    return 0;
}

static int
update_display(hwc_context_t * ctx, int disp, hwc_display_contents_1_t * display)
{
    kms_display_t *kdisp = &ctx->displays[disp];

    if (kdisp->compo_updated) {
        /* wait 20 ms */
        ALOGI_IF(DEBUG_ST_HWCOMPOSER, "Pending job, wait for completion");
        usleep(20000);

        if (kdisp->compo_updated)
            ALOGW("Job still pending (too long)");
    }

    return update_display_locked(ctx, disp, display);
}

static int
hwc_set(struct hwc_composer_device_1 *dev, size_t numDisplays, hwc_display_contents_1_t ** displays)
{
    if (!numDisplays || !displays)
        return 0;

    hwc_display_contents_1_t *content = displays[HWC_DISPLAY_PRIMARY];
    int ret = 0;
    hwc_context_t *ctx = to_ctx(dev);
    size_t i = 0;

    if (content) {
        ret = update_display(ctx, HWC_DISPLAY_PRIMARY, content);
        if (ret)
            return ret;
    }

    content = displays[HWC_DISPLAY_EXTERNAL];
    if (content)
        return update_display(ctx, HWC_DISPLAY_EXTERNAL, content);

    return ret;
}

static int
find_plane(hwc_context_t * ctx, int disp, private_handle_t * hnd)
{
    unsigned int i, j, drm_plane_id = 0;
    drmModePlaneResPtr plane_res;
    int drm_fd = ctx->drm_fd;
    unsigned int fourcc = hnd_to_fourcc(hnd);

    if (!fourcc) {
        ALOGI("no plane fourcc for handle %08x", intptr_t(hnd));
        return drm_plane_id;
    }

    plane_res = drmModeGetPlaneResources(drm_fd);

    ctx->nb_planes = MIN(plane_res->count_planes, MAX_DRM_PLANES);

    for (i = 0; i < ctx->nb_planes && !drm_plane_id; i++) {
        drmModePlanePtr plane;

        plane = drmModeGetPlane(drm_fd, plane_res->planes[i]);

        if (!plane)
            continue;

        if (!(plane->possible_crtcs & (1 << disp))) {
            drmModeFreePlane(plane);
            continue;
        }

        if (ctx->used_planes & (1 << i)) {
            drmModeFreePlane(plane);
            continue;
        }

        for (j = 0; j < plane->count_formats && !drm_plane_id; j++) {
            if (plane->formats[j] == fourcc) {
                drm_plane_id = plane->plane_id;
                hnd->plane_id = plane->plane_id;
                ctx->used_planes |= 1 << i;
                ctx->plane_id[i] = plane->plane_id;
            }
        }

        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(plane_res);

    return drm_plane_id;
}

#define MAX_SIZE_CURSOR 64

static int
prepare_display(hwc_context_t * ctx, int disp, hwc_display_contents_1_t * content)
{
    kms_display_t *d = &ctx->displays[disp];
    bool target_framebuffer = false;
    bool is_top = true;

    if (!is_display_connected(ctx, disp))
        return 0;

    ALOGI_IF(DEBUG_ST_HWCOMPOSER, "PREPARE (%d layers)", content->numHwLayers);

    for (int i = content->numHwLayers - 1; i >= 0; i--) {
        hwc_layer_1_t & layer = content->hwLayers[i];
        private_handle_t *hnd = (private_handle_t *) layer.handle;

        dump_layer(&layer, (int) i);

        if (layer.flags & HWC_SKIP_LAYER)
            continue;

        if (layer.compositionType == HWC_FRAMEBUFFER_TARGET)
            continue;

        if (target_framebuffer) {
            layer.compositionType = HWC_FRAMEBUFFER;
            is_top = false;
            continue;
        }

        if (d->cursor.support && is_top &&
                ((layer.displayFrame.right - layer.displayFrame.left) < MAX_SIZE_CURSOR) &&
                ((layer.displayFrame.bottom - layer.displayFrame.top) < MAX_SIZE_CURSOR)) {
            layer.compositionType = HWC_CURSOR_OVERLAY;
            is_top = false;
            continue;
        }

        if (find_plane(ctx, disp, hnd)) {
            layer.compositionType = HWC_OVERLAY;
            is_top = false;
            continue;
        }

        layer.compositionType = HWC_FRAMEBUFFER;
        is_top = false;
        target_framebuffer = true;
    }

    return 0;
}

static int
hwc_prepare(struct hwc_composer_device_1 *dev,
        size_t numDisplays, hwc_display_contents_1_t ** displays)
{
    if (!numDisplays || !displays)
        return 0;

    hwc_display_contents_1_t *content = displays[HWC_DISPLAY_PRIMARY];
    hwc_context_t *ctx = to_ctx(dev);
    int ret = 0;

    ctx->used_planes = 0;

    if (content) {
        ret = prepare_display(ctx, HWC_DISPLAY_PRIMARY, content);

        if (ret)
            return ret;
    }

    /* do not use planes for external display */
    ctx->used_planes = -1;
    content = displays[HWC_DISPLAY_EXTERNAL];
    if (content)
        ret = prepare_display(ctx, HWC_DISPLAY_EXTERNAL, content);

    return ret;
}

static int
hwc_eventControl(struct hwc_composer_device_1 *dev, int disp, int event, int enabled)
{
    hwc_context_t *ctx = to_ctx(dev);

    if (disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES)
        return -EINVAL;

    switch (event) {
        case HWC_EVENT_VSYNC:
            ctx->displays[disp].vsync_on = enabled;
            return 0;
        default:
            return -EINVAL;
    }
}

static int
hwc_query(struct hwc_composer_device_1 *dev, int what, int *value)
{
    hwc_context_t *ctx = to_ctx(dev);
    int refreshRate = 60;

    switch (what) {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            value[0] = 0;
            break;
        case HWC_VSYNC_PERIOD:
            value[0] = 1000000000 / refreshRate;
            break;
        case HWC_DISPLAY_TYPES_SUPPORTED:
            if (is_display_connected(ctx, HWC_DISPLAY_PRIMARY))
                value[0] = HWC_DISPLAY_PRIMARY_BIT;
            if (is_display_connected(ctx, HWC_DISPLAY_EXTERNAL))
                value[0] |= HWC_DISPLAY_EXTERNAL_BIT;
            break;
        default:
            return -EINVAL;     //unsupported query
    }
    return 0;
}

static void
hwc_registerProcs(struct hwc_composer_device_1 *dev, hwc_procs_t const *procs)
{
    hwc_context_t *ctx = to_ctx(dev);

    ctx->cb_procs = procs;
}

static int
hwc_getDisplayConfigs(struct hwc_composer_device_1 *dev,
        int disp, uint32_t * configs, size_t * numConfigs)
{
    hwc_context_t *ctx = to_ctx(dev);

    if (*numConfigs == 0)
        return 0;

    if (is_display_connected(ctx, disp)) {
        configs[0] = HWC_DEFAULT_CONFIG;
        *numConfigs = 1;
        return 0;
    }
    return -EINVAL;
}

static int
hwc_getDisplayAttributes(struct hwc_composer_device_1 *dev, int disp,
        uint32_t config, const uint32_t * attributes, int32_t * values)
{
    hwc_context_t *ctx = to_ctx(dev);
    kms_display_t *d = &ctx->displays[disp];

    if (!is_display_connected(ctx, disp))
        return -EINVAL;

    if (config != HWC_DEFAULT_CONFIG)
        return -EINVAL;

    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch (attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = 1000000000 / 60;
                break;
            case HWC_DISPLAY_WIDTH:
                values[i] = d->mode->hdisplay;
                break;
            case HWC_DISPLAY_HEIGHT:
                values[i] = d->mode->vdisplay;
                break;
            case HWC_DISPLAY_DPI_X:
                values[i] = 0;
                if (d->con->mmWidth)
                    values[i] = (d->mode->hdisplay * 25400) / d->con->mmWidth;
                break;
            case HWC_DISPLAY_DPI_Y:
                values[i] = 0;
                if (d->con->mmHeight)
                    values[i] = (d->mode->vdisplay * 25400) / d->con->mmHeight;
                break;
            default:
                ALOGE("unknown display attribute %u", *attributes);
                values[i] = 0;
                return -EINVAL;
        }
    }
    return 0;
}

static int
hwc_blank(struct hwc_composer_device_1 *dev, int disp, int blank)
{
    hwc_context_t *ctx = to_ctx(dev);
    int arg, ret;

    if (!is_display_connected(ctx, disp))
        return -EINVAL;

    arg = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;

    ret = ioctl(ctx->drm_fd, FBIOBLANK, arg);
    return ret;
}

static void
hwc_dump(struct hwc_composer_device_1 *dev, char *buff, int buff_len)
{
    (void) dev, buff, buff_len;
    ALOGD("%s", __func__);
}

static int
hwc_device_close(struct hw_device_t *dev)
{
    hwc_context_t *ctx = to_ctx(dev);

    if (!ctx)
        return 0;

    destroy_display(&ctx->displays[HWC_DISPLAY_PRIMARY]);
    destroy_display(&ctx->displays[HWC_DISPLAY_EXTERNAL]);

    drmClose(ctx->drm_fd);
    free(ctx);

    return 0;
}

static void
init_gralloc(int drm_fd)
{
    hw_module_t *pmodule = NULL;
    private_module_t *m = NULL;
    hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **) &pmodule);
    m = reinterpret_cast < private_module_t * >(pmodule);
    m->drm_fd = drm_fd;
}

static int
hwc_get_connector(char *conn_str)
{
    uint32_t i;

    /* Property not set or empty */
    if (conn_str[0] == '\0')
        return DRM_MODE_CONNECTOR_Unknown;

    /* Disabled display */
    if (!strncasecmp(conn_str, "OFF", 64))
        return -1;

    /* Look for known connectors */
    for (i = 0; i < ARRAY_SIZE(connector_list); i++) {
        if (!strncasecmp(conn_str, connector_list[i].name, 64))
            return connector_list[i].type;
    }

    /* Add this useful case */
    if (!strncasecmp(conn_str, "HDMI", 64))
        return DRM_MODE_CONNECTOR_HDMIA;

    ALOGE("Unknown connector (%s), will use default", conn_str);
    return DRM_MODE_CONNECTOR_Unknown;
}

static int
hwc_device_open(const struct hw_module_t *module, const char *name, struct hw_device_t **device)
{
    hwc_context_t *ctx;
    drmModeResPtr resources;
    drmModePlaneResPtr planes;
    int err = 0;
    int ret = 0;
    int drm_fd = 0;
    int connector;
    char prop_val[PROPERTY_VALUE_MAX];

    if (strcmp(name, HWC_HARDWARE_COMPOSER))
        return -EINVAL;
    ctx = (hwc_context_t *) calloc(1, sizeof(*ctx));

    /* Initialize the procs */
    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = HWC_DEVICE_API_VERSION_1_1;
    ctx->device.common.module = (struct hw_module_t *) module;
    ctx->device.common.close = hwc_device_close;

    ctx->device.prepare = hwc_prepare;
    ctx->device.set = hwc_set;
    ctx->device.eventControl = hwc_eventControl;
    ctx->device.blank = hwc_blank;
    ctx->device.query = hwc_query;
    ctx->device.registerProcs = hwc_registerProcs;
    ctx->device.dump = hwc_dump;
    ctx->device.getDisplayConfigs = hwc_getDisplayConfigs;
    ctx->device.getDisplayAttributes = hwc_getDisplayAttributes;

    ctx->drm_fd = -1;

    /* Open Gralloc module */
    ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const struct hw_module_t **) &ctx->gralloc);
    if (ret) {
        ALOGE("Failed to get gralloc module: %s", strerror(errno));
        return ret;
    }

    property_get("ro.disp.conn.primary", prop_val, "");
    connector = hwc_get_connector(prop_val);
    ret = init_display(ctx, HWC_DISPLAY_PRIMARY, connector);
    if (ret) {
        if (ctx->drm_fd != -1)
            drmClose(ctx->drm_fd);
        return -EINVAL;
    }

    property_get("ro.disp.conn.external", prop_val, "OFF");
    connector = hwc_get_connector(prop_val);
    if (connector >= 0)
        init_display(ctx, HWC_DISPLAY_EXTERNAL, connector);

    ctx->used_planes = 0;

    init_gralloc(ctx->drm_fd);

    pthread_attr_t attrs;
    pthread_attr_init(&attrs);
    pthread_attr_setdetachstate(&attrs, PTHREAD_CREATE_JOINABLE);
    ret = pthread_create(&ctx->event_thread, &attrs, event_handler, ctx);
    if (ret) {
        ALOGE("Failed to create event thread:%s", strerror(ret));
        ret = -ret;
        return ret;
    }

    *device = &ctx->device.common;

    return 0;
}

static struct hw_module_methods_t hwc_module_methods = {
  open:hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
  common:{
      tag:     HARDWARE_MODULE_TAG,
      module_api_version:HWC_MODULE_API_VERSION_0_1,
      hal_api_version:HARDWARE_HAL_API_VERSION,
      id:      HWC_HARDWARE_MODULE_ID,
      name:    "DRM/KMS hwcomposer module",
      author:  "Benjamin Gaignard <benjamin.gaignard@linaro.org>",
      methods: &hwc_module_methods,
      dso:     0,
      reserved:{0},
            }
};
