/*
 * Copyright (C) 2008 The Android Open Source Project
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


#define LOG_TAG "copybit"

#include <cutils/log.h>

#include <linux/msm_mdp.h>
#include <linux/fb.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <hardware/copybit.h>
#include <stdlib.h>

#include <cutils/properties.h>

#include "gralloc_priv.h"

#include "copybit.h"

#define DEBUG_MDP_ERRORS 0

/******************************************************************************/

#define MAX_SCALE_FACTOR    (4)
#define MAX_DIMENSION       (4096)

/******************************************************************************/

/** State information for each device instance */
struct copybit_context_t {
    struct copybit_device_t device;
    int     mFD;
    uint8_t mAlpha;
    uint8_t mFlags;
};

/**
 * Common hardware methods
 */

static int open_copybit(const struct hw_module_t* module, const char* name,
                        struct hw_device_t** device);

static struct hw_module_methods_t copybit_module_methods = {
    open: open_copybit
};

/*
 * The COPYBIT Module
 */
struct copybit_module_t HAL_MODULE_INFO_SYM = {
    common:
    {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: COPYBIT_HARDWARE_MODULE_ID,
        name: "Atmel SAM9X COPYBIT Module",
        author: "Embest, Inc.",
        methods: &copybit_module_methods
    }
};

/******************************************************************************/

/** min of int a, b */
static inline int min(int a, int b)
{
    return (a<b) ? a : b;
}

/** max of int a, b */
static inline int max(int a, int b)
{
    return (a>b) ? a : b;
}

/** scale each parameter by mul/div. Assume div isn't 0 */
static inline void MULDIV(uint32_t *a, uint32_t *b, int mul, int div)
{
    if (mul != div) {
        *a = (mul * *a) / div;
        *b = (mul * *b) / div;
    }
}

/** Determine the intersection of lhs & rhs store in out */
static void intersect(struct copybit_rect_t *out,
                      const struct copybit_rect_t *lhs,
                      const struct copybit_rect_t *rhs)
{
    out->l = max(lhs->l, rhs->l);
    out->t = max(lhs->t, rhs->t);
    out->r = min(lhs->r, rhs->r);
    out->b = min(lhs->b, rhs->b);
}

/** convert COPYBIT_FORMAT to MDP format */
static int get_format(int format)
{
    switch (format) {
    case COPYBIT_FORMAT_RGB_565:
        return MDP_RGB_565;
    case COPYBIT_FORMAT_RGBX_8888:
        return MDP_RGBX_8888;
    case COPYBIT_FORMAT_RGB_888:
        return MDP_RGB_888;
    case COPYBIT_FORMAT_RGBA_8888:
        return MDP_RGBA_8888;
    case COPYBIT_FORMAT_BGRA_8888:
        return MDP_BGRA_8888;
    case COPYBIT_FORMAT_YCrCb_420_SP:
        return MDP_Y_CBCR_H2V2;
    case COPYBIT_FORMAT_YCbCr_422_SP:
        return MDP_Y_CRCB_H2V1;
    }
    return -1;
}

static int get_bpp(int format)
{
    switch (format) {
    case MDP_RGB_565:
        return 2;
    case MDP_RGBX_8888:
    case MDP_RGBA_8888:
    case MDP_BGRA_8888:
        return 4;
    case MDP_RGB_888:
        return 3;
    }
    return -1;
}

/** convert from copybit image to mdp image structure */
static void set_image(struct mdp_img_atmel *img, const struct copybit_image_t *rhs)
{
    private_handle_t* hnd = (private_handle_t*)rhs->handle;
    img->width      = rhs->w;
    img->height     = rhs->h;
    img->format     = get_format(rhs->format);
    img->offset     = hnd->offset;
    img->base      = (void*)hnd->base;
    img->memory_id  = hnd->fd;
}
/** setup rectangles */
static void set_rects(struct copybit_context_t *dev,
                      struct mdp_blit_req_atmel *e,
                      const struct copybit_rect_t *dst,
                      const struct copybit_rect_t *src,
                      const struct copybit_rect_t *scissor)
{
    struct copybit_rect_t clip;
    intersect(&clip, scissor, dst);

    e->dst_rect.x  = clip.l;
    e->dst_rect.y  = clip.t;
    e->dst_rect.w  = clip.r - clip.l;
    e->dst_rect.h  = clip.b - clip.t;

    uint32_t W, H;
    if (dev->mFlags & COPYBIT_TRANSFORM_ROT_90) {
        e->src_rect.x = (clip.t - dst->t) + src->t;
        e->src_rect.y = (dst->r - clip.r) + src->l;
        e->src_rect.w = (clip.b - clip.t);
        e->src_rect.h = (clip.r - clip.l);
        W = dst->b - dst->t;
        H = dst->r - dst->l;
    } else {
        e->src_rect.x  = (clip.l - dst->l) + src->l;
        e->src_rect.y  = (clip.t - dst->t) + src->t;
        e->src_rect.w  = (clip.r - clip.l);
        e->src_rect.h  = (clip.b - clip.t);
        W = dst->r - dst->l;
        H = dst->b - dst->t;
    }
    MULDIV(&e->src_rect.x, &e->src_rect.w, src->r - src->l, W);
    MULDIV(&e->src_rect.y, &e->src_rect.h, src->b - src->t, H);
    if (dev->mFlags & COPYBIT_TRANSFORM_FLIP_V) {
        e->src_rect.y = e->src.height - (e->src_rect.y + e->src_rect.h);
    }
    if (dev->mFlags & COPYBIT_TRANSFORM_FLIP_H) {
        e->src_rect.x = e->src.width  - (e->src_rect.x + e->src_rect.w);
    }
}

/** setup mdp request */
static void set_infos(struct copybit_context_t *dev, struct mdp_blit_req_atmel *req)
{
    req->alpha = dev->mAlpha;
    req->transp_mask = MDP_TRANSP_NOP;
    req->flags = dev->mFlags | MDP_BLEND_FG_PREMULT;
}

/** copy the bits */
static int atmel_copybit(struct copybit_context_t *dev, void const *list)
{
    struct mdp_blit_req_list_atmel const* l = (struct mdp_blit_req_list_atmel const*)list;
    const size_t src_bpp = get_bpp(l->req[0].src.format);
    const size_t dst_bpp = get_bpp(l->req[0].dst.format);
    if(src_bpp != dst_bpp) {
        LOGE_IF(DEBUG_MDP_ERRORS,"In atmel_copybit, src format != dst format, we just return and let opengl do the convert");
        return -EINVAL;
    }
    const size_t bpp = src_bpp;
    for (int i=0 ; i<l->count ; i++) {
        LOGD_IF(DEBUG_MDP_ERRORS,"%d: src={w=%d, h=%d, f=%d, membase=%p, rect={%d,%d,%d,%d}}\n"
                "    dst={w=%d, h=%d, f=%d, membase=%p, rect={%d,%d,%d,%d}}\n"
                "    flags=%08lx, transp_mask=%08lx, alpha= %d"
                ,
                i,
                l->req[i].src.width,
                l->req[i].src.height,
                l->req[i].src.format,
                l->req[i].src.base,
                l->req[i].src_rect.x,
                l->req[i].src_rect.y,
                l->req[i].src_rect.w,
                l->req[i].src_rect.h,
                l->req[i].dst.width,
                l->req[i].dst.height,
                l->req[i].dst.format,
                l->req[i].dst.base,
                l->req[i].dst_rect.x,
                l->req[i].dst_rect.y,
                l->req[i].dst_rect.w,
                l->req[i].dst_rect.h,
                l->req[i].flags,
                l->req[i].transp_mask,
                l->req[i].alpha
               );
        mdp_rect rs = l->req[i].src_rect;
        mdp_rect rd = l->req[i].dst_rect;

        ssize_t w = (rs.w < rd.w) ? rs.w : rd.w;
        ssize_t h = (rs.h < rd.h ) ? rs.h : rd.h;
        if (w <= 0 || h<=0) continue;
        uint8_t const * const src_bits = (uint8_t const *)l->req[i].src.base;
        uint8_t       * const  dst_bits = (uint8_t       *)l->req[i].dst.base;
        size_t size = w * bpp;
        const size_t dbpr = (l->req[i].dst.width) * bpp;
        const size_t sbpr = (l->req[i].src.width) * bpp;

        uint8_t const * s = src_bits + (rs.x + (l->req[i].src.width) * rs.y) * bpp;
        /**
          * This is for 9M10 video play
          * when paly low resolution <48 * 48>, this will help the video be placed in the middle
          */
        if(rd.w > rs.w)
            rd.x += (rd.w - rs.w) / 2;
        if(rd.h > rs.h)
            rd.y += (rd.h - rs.h) /2;
        uint8_t       * d = dst_bits + (rd.x + (l->req[i].dst.width) * rd.y) * bpp;

        if (dbpr==sbpr && size==sbpr) {
            size *= h;
            h = 1;
        }
        do {
            memcpy(d, s, size);
            d += dbpr;
            s += sbpr;
        } while (--h > 0);
    }
    return 0;
}

/*****************************************************************************/

/** Set a parameter to value */
static int set_parameter_copybit(
    struct copybit_device_t *dev,
    int name,
    int value)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int status = 0;
    if (ctx) {
        switch(name) {
        case COPYBIT_ROTATION_DEG:
            switch (value) {
            case 0:
                ctx->mFlags &= ~0x7;
                break;
            case 90:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= MDP_ROT_90;
                break;
            case 180:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= MDP_ROT_180;
                break;
            case 270:
                ctx->mFlags &= ~0x7;
                ctx->mFlags |= MDP_ROT_270;
                break;
            default:
                LOGE("Invalid value for COPYBIT_ROTATION_DEG");
                status = -EINVAL;
                break;
            }
            break;
        case COPYBIT_PLANE_ALPHA:
            if (value < 0)      value = 0;
            if (value >= 256)   value = 255;
            ctx->mAlpha = value;
            break;
        case COPYBIT_DITHER:
            if (value == COPYBIT_ENABLE) {
                /* This is for video play
                  * In onDraw@LayerBuffer.cpp
                  * We want to use memcpy in such case
                  */
                if((ctx->mAlpha == 0xFF) && ((ctx->mFlags & 0x7) == 0))
                    ctx->mFlags &= ~MDP_DITHER;
                else
                    ctx->mFlags |= MDP_DITHER;
            } else if (value == COPYBIT_DISABLE) {
                ctx->mFlags &= ~MDP_DITHER;
            }
            break;
        case COPYBIT_BLUR:
            if (value == COPYBIT_ENABLE) {
                ctx->mFlags |= MDP_BLUR;
            } else if (value == COPYBIT_DISABLE) {
                ctx->mFlags &= ~MDP_BLUR;
            }
            break;
        case COPYBIT_TRANSFORM:
            ctx->mFlags &= ~0x7;
            ctx->mFlags |= value & 0x7;
            break;
        default:
            status = -EINVAL;
            break;
        }
    } else {
        status = -EINVAL;
    }
    return -EINVAL;
}

/** Get a static info value */
static int get(struct copybit_device_t *dev, int name)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int value;
    if (ctx) {
        switch(name) {
        case COPYBIT_MINIFICATION_LIMIT:
            value = MAX_SCALE_FACTOR;
            break;
        case COPYBIT_MAGNIFICATION_LIMIT:
            value = MAX_SCALE_FACTOR;
            break;
        case COPYBIT_SCALING_FRAC_BITS:
            value = 32;
            break;
        case COPYBIT_ROTATION_STEP_DEG:
            value = 90;
            break;
        default:
            value = -EINVAL;
        }
    } else {
        value = -EINVAL;
    }
    return value;
}

/** do a stretch blit type operation */
static int stretch_copybit(
    struct copybit_device_t *dev,
    struct copybit_image_t const *dst,
    struct copybit_image_t const *src,
    struct copybit_rect_t const *dst_rect,
    struct copybit_rect_t const *src_rect,
    struct copybit_region_t const *region)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int status = 0;
    if (ctx) {
        struct {
            uint32_t count;
            struct mdp_blit_req_atmel req[12];
        } list;

        if (ctx->mAlpha < 255) {
            return -EINVAL;
        }

        if (ctx->mFlags != MDP_ROT_NOP) {
            LOGE_IF(DEBUG_MDP_ERRORS,"The flag is 0x%x",ctx->mFlags);
            return -EINVAL;
        }

        if (src_rect->l < 0 || src_rect->r > src->w ||
                src_rect->t < 0 || src_rect->b > src->h) {
            return -EINVAL;
        }

        if (dst->w > src->w || dst->h > src->h)  {
            return -EINVAL;
        }

        if (src->w > MAX_DIMENSION || src->h > MAX_DIMENSION)
            return -EINVAL;

        if (dst->w > MAX_DIMENSION || dst->h > MAX_DIMENSION)
            return -EINVAL;

        const uint32_t maxCount = sizeof(list.req)/sizeof(list.req[0]);
        const struct copybit_rect_t bounds = { 0, 0, dst->w, dst->h };
        struct copybit_rect_t clip;
        list.count = 0;
        status = 0;
        while ((status == 0) && region->next(region, &clip)) {
            intersect(&clip, &bounds, &clip);
            mdp_blit_req_atmel *req = &list.req[list.count];
            set_infos(ctx, req);
            set_image(&req->dst, dst);
            set_image(&req->src, src);
            set_rects(ctx, req, dst_rect, src_rect, &clip);

            if (req->src_rect.w<=0 || req->src_rect.h<=0)
                continue;

            if (req->dst_rect.w<=0 || req->dst_rect.h<=0)
                continue;

            if (++list.count == maxCount) {
                status = atmel_copybit(ctx, &list);
                list.count = 0;
            }
        }
        if ((status == 0) && list.count) {
            status = atmel_copybit(ctx, &list);
        }
    } else {
        status = -EINVAL;
    }
    return status;
}

/** Perform a blit type operation */
static int blit_copybit(
    struct copybit_device_t *dev,
    struct copybit_image_t const *dst,
    struct copybit_image_t const *src,
    struct copybit_region_t const *region)
{
    struct copybit_rect_t dr = { 0, 0, dst->w, dst->h };
    struct copybit_rect_t sr = { 0, 0, src->w, src->h };
    return stretch_copybit(dev, dst, src, &dr, &sr, region);
}

/*****************************************************************************/

/** Close the copybit device */
static int close_copybit(struct hw_device_t *dev)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    if (ctx) {
        /*close(ctx->mFD);*/
        free(ctx);
    }
    return 0;
}

/** Open a new instance of a copybit device using name */
static int open_copybit(const struct hw_module_t* module, const char* name,
                        struct hw_device_t** device)
{
    int status = 0;
    copybit_context_t *ctx;
    ctx = (copybit_context_t *)malloc(sizeof(copybit_context_t));
    memset(ctx, 0, sizeof(*ctx));

    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = 1;
    ctx->device.common.module = const_cast<hw_module_t*>(module);
    ctx->device.common.close = close_copybit;
    ctx->device.set_parameter = set_parameter_copybit;
    ctx->device.get = get;
    ctx->device.blit = blit_copybit;
    ctx->device.stretch = stretch_copybit;
    ctx->mAlpha = MDP_ALPHA_NOP;
    ctx->mFlags = 0;
    ctx->mFD = 0;//open("/dev/graphics/fb0", O_RDWR, 0);


    if (status == 0) {
        *device = &ctx->device.common;
    } else {
        close_copybit(&ctx->device.common);
    }
    return status;
}
