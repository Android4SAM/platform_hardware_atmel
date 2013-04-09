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

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>

#include <EGL/egl.h>

#include "common.h"
#include "gralloc_priv.h"

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common:
    {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Atmel SAM9X5 hwcomposer module",
        author: "ATMEL",
        methods: &hwc_module_methods,
    }
};

/*****************************************************************************/

static void dump_layer(hwc_layer_t const* l) {
    LOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
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

static int copy_src_content(hwc_layer_t *cur_layer,
                            struct hwc_win_info_t *win,
                            int win_idx)
{
    /* switch to next buffer
      * buf_index will be reset to NUM_OF_WIN_BUF when win be open
      */
    if (win->set_win_flag == 1)
    	win->buf_index = (win->buf_index + 1) % NUM_OF_WIN_BUF;

    private_handle_t *prev_handle = (private_handle_t *)(cur_layer->handle);
    hwc_rect_t *cur_rect = (hwc_rect_t *)cur_layer->visibleRegionScreen.rects;
    uint8_t *dst_addr = (uint8_t *)win->vir_addr[win->buf_index];
    uint8_t *src_addr = (uint8_t *)prev_handle->base;
    uint32_t cpy_size = 0;

    if(MAX_NUM_OF_WIN <= win_idx)
        return -1;

    for (unsigned int i = 0; i < cur_layer->visibleRegionScreen.numRects; i++) {
        int w = cur_rect->right - cur_rect->left;
        int h = cur_rect->bottom - cur_rect->top;
        uint8_t *cur_dst_addr = dst_addr;
        uint8_t *cur_src_addr = &src_addr[((cur_rect->top - cur_layer->displayFrame.top) *
                                           (cur_layer->displayFrame.right - cur_layer->displayFrame.left) +
                                           (cur_rect->left - cur_layer->displayFrame.left)) * (prev_handle->uiBpp / 8)];

        if (w == (cur_layer->displayFrame.right - cur_layer->displayFrame.left)) {
            cpy_size= w * (prev_handle->uiBpp / 8) * h;
            h = 1;
        } else {
            cpy_size= w * (prev_handle->uiBpp / 8);
        }

        for (int j = 0; j < h ; j++) {
            memcpy(cur_dst_addr, cur_src_addr, cpy_size);
            //cur_dst_addr = &cur_dst_addr[win->fix_info.line_length];
            cur_dst_addr = &cur_dst_addr[cpy_size];
            cur_src_addr = &cur_src_addr[(cur_layer->displayFrame.right - cur_layer->displayFrame.left) * (prev_handle->uiBpp / 8)];
        }

        cur_rect++;
    }

    return 0;
}

static int copy_heo_src_content(hwc_layer_t *cur_layer,
                                struct hwc_win_info_t_heo *win,
                                int win_idx)
{
    private_handle_t *prev_handle = (private_handle_t *)(cur_layer->handle);
    hwc_rect_t *cur_rect = (hwc_rect_t *)cur_layer->visibleRegionScreen.rects;
    uint8_t *dst_addr = (uint8_t *)win->buffers[win->buf_index];
    uint8_t *src_addr = (uint8_t *)prev_handle->base;
    uint32_t cpy_size = 0;
    uint32_t BPP = 0;
    int w = win->video_width;
    int h = win->video_height;

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
        LOGE("%s, heo don't support this format", __func__);
        return 0;
    }

    for (unsigned int i = 0; i < cur_layer->visibleRegionScreen.numRects; i++) {
        uint8_t *cur_dst_addr = dst_addr;
        uint8_t *cur_src_addr = src_addr;

        for (int j = 0; j < h ; j++) {
            memcpy(cur_dst_addr, cur_src_addr, cpy_size);
            cur_dst_addr = &cur_dst_addr[cpy_size];
            cur_src_addr = &cur_src_addr[(cur_layer->displayFrame.right - cur_layer->displayFrame.left) * (prev_handle->uiBpp / 8)];
        }
        cur_rect++;
    }

    if (v4l2_overlay_q_buf( win->fd, win->buf_index, win->zero_copy) < 0) {
        LOGE("%s:Failed to Qbuf", __func__);
        return -1;
    } else {
        win->qd_buf_count++;
    }

    if (win->qd_buf_count < win->num_of_buffer) {
        win->buf_index = (win->buf_index + 1) % NUM_OF_HEO_BUF;
        return 0;
    }

    if (v4l2_overlay_dq_buf( win->fd, &win->buf_index,win->zero_copy) < 0) {
        LOGE("%s:Failed to Qbuf", __func__);
        return -1;
    } else {
        win->qd_buf_count--;
    }

    return 0;
}

static void reset_win_rect_info(hwc_win_info_t *win)
{
    win->rect_info.x = 0;
    win->rect_info.y = 0;
    win->rect_info.w = 0;
    win->rect_info.h = 0;
    if (window_reset_pos(win) < 0) {
        LOGE("%s::window_set_pos is failed : %s",
             __func__, strerror(errno));
    }
    return;
}

static void reset_heo_win_rect_info(hwc_win_info_t_heo *win)
{
    int ret = 0;
    ret = v4l2_overlay_stream_off(win);
    if (ret) {
        LOGE("%s: steam off error", __func__);
    } else {
        win->set_win_flag = 1;
        win->qd_buf_count = 0;
    }
    return;
}

static int get_hwc_compos_decision(hwc_layer_t* cur, int *usage)
{
    if (cur->flags & HWC_SKIP_LAYER || !cur->handle) {
        LOGV("%s::is_skip_layer %d cur->handle %x",
             __func__, cur->flags & HWC_SKIP_LAYER, (uint32_t)cur->handle);
        return HWC_FRAMEBUFFER;
    }

    private_handle_t *prev_handle = (private_handle_t *)(cur->handle);
    int compositionType = HWC_FRAMEBUFFER;

    /* check here....if we have any resolution constraints */
    if (((cur->sourceCrop.right - cur->sourceCrop.left) < 16) ||
            ((cur->sourceCrop.bottom - cur->sourceCrop.top) < 8))
        return compositionType;

    if ((cur->transform == HAL_TRANSFORM_ROT_90) ||
            (cur->transform == HAL_TRANSFORM_ROT_270)) {
        if (((cur->displayFrame.right - cur->displayFrame.left) < 4)||
                ((cur->displayFrame.bottom - cur->displayFrame.top) < 8))
            return compositionType;
    } else if (((cur->displayFrame.right - cur->displayFrame.left) < 8) ||
               ((cur->displayFrame.bottom - cur->displayFrame.top) < 4))
        return compositionType;

    if (cur->visibleRegionScreen.numRects != 1)
        return compositionType;

    /* We only handle RGBA8888 data here */
    switch (prev_handle->iFormat) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
        if (((cur->sourceCrop.right - cur->sourceCrop.left) != (cur->displayFrame.right - cur->displayFrame.left))
                ||
                ((cur->sourceCrop.bottom - cur->sourceCrop.top) != (cur->displayFrame.bottom - cur->displayFrame.top))) {
            compositionType = HWC_FRAMEBUFFER;
            LOGE("%s :: We need to scaler, didn't support now!", __func__);
        } else {
            compositionType = HWC_OVERLAY;
            *usage = USE_OVR;
            LOGV("%s::compositionType %d bpp %d format %x",
                 __func__,compositionType, prev_handle->uiBpp, prev_handle->iFormat);
        }
        break;
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
        compositionType = HWC_OVERLAY;
        *usage = USE_HEO;
        LOGV("%s::compositionType %d bpp %d format %x",
             __func__,compositionType, prev_handle->uiBpp, prev_handle->iFormat);
        break;
    default :
        compositionType = HWC_FRAMEBUFFER;
        LOGV("%s didn't support :: bpp %d format %x",
             __func__, prev_handle->uiBpp, prev_handle->iFormat);
        break;
    }

    return  compositionType;
}

static int assign_overlay_window(struct hwc_context_t *ctx,
                                 hwc_layer_t *cur,
                                 int win_idx,
                                 int layer_idx)
{
    struct hwc_win_info_t *win;
    sam_rect rect;
    int ret = 0;

    if (ctx->num_of_avail_ovl <= win_idx)
        return -1;

    private_handle_t *prev_handle = (private_handle_t *)(cur->handle);
    hwc_rect_t *visible_rect = (hwc_rect_t *)cur->visibleRegionScreen.rects;
    win = &ctx->win[win_idx];

    win->var_info.bits_per_pixel = prev_handle->uiBpp;
    rect.x = SAM_MAX(visible_rect->left, 0);
    rect.y = SAM_MAX(visible_rect->top, 0);
    rect.w = SAM_MIN(visible_rect->right - rect.x, win->lcd_info.xres - rect.x);
    rect.h = SAM_MIN(visible_rect->bottom - rect.y, win->lcd_info.yres - rect.y);
    win->set_win_flag = 0;

    if ((rect.x != win->rect_info.x) || (rect.y != win->rect_info.y) ||
            (rect.w != win->rect_info.w) || (rect.h != win->rect_info.h) ||
            (prev_handle->iFormat != win->layer_prev_format)) {
        win->rect_info.x = rect.x;
        win->rect_info.y = rect.y;
        win->rect_info.w = rect.w;
        win->rect_info.h = rect.h;
        win->layer_prev_format = prev_handle->iFormat;
        win->set_win_flag = 1;
        win->layer_prev_buf = 0;
        switch (prev_handle->iFormat) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            win->transp_offset = 24;
            break;
        }
    }

    win->layer_index = layer_idx;
    win->status = HWC_WIN_RESERVED;

    LOGV("%s:: win_x %d win_y %d win_w %d win_h %d lay_idx %d win_idx %d iFormat %d",
         __func__, win->rect_info.x, win->rect_info.y, win->rect_info.w,
         win->rect_info.h, win->layer_index, win_idx, prev_handle->iFormat );

    return 0;
}

static int assign_heo_overlay_window(struct hwc_context_t *ctx,
                                     hwc_layer_t *cur,
                                     int win_idx,
                                     int layer_idx)
{
    struct hwc_win_info_t_heo *win;
    sam_rect rect;
    int ret = 0;

    if (ctx->num_of_avail_heo <= win_idx)
        return -1;

    private_handle_t *prev_handle = (private_handle_t *)(cur->handle);
    hwc_rect_t *visible_rect = (hwc_rect_t *)cur->visibleRegionScreen.rects;
    win = &ctx->win_heo[win_idx];

    rect.x = SAM_MAX(visible_rect->left, 0);
    rect.y = SAM_MAX(visible_rect->top, 0);
    rect.w = SAM_MIN(visible_rect->right - rect.x, win->lcd_info.xres - rect.x);
    rect.h = SAM_MIN(visible_rect->bottom - rect.y, win->lcd_info.yres - rect.y);

    if ((rect.x != win->rect_info.x) || (rect.y != win->rect_info.y) ||
            (rect.w != win->rect_info.w) || (rect.h != win->rect_info.h) ||
            (prev_handle->iFormat != win->layer_prev_format)) {
        win->rect_info.x = rect.x;
        win->rect_info.y = rect.y;
        win->rect_info.w = rect.w;
        win->rect_info.h = rect.h;
        win->layer_prev_format = prev_handle->iFormat;
        win->set_win_flag = 1;
        win->layer_prev_buf = 0;
        switch (prev_handle->iFormat) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
            win->transp_offset = 24;
            break;
        }
    }

    if ((cur->sourceCrop.right - cur->sourceCrop.left) != win->video_width ||
            (cur->sourceCrop.bottom - cur->sourceCrop.top) != win->video_width) {
        win->video_width = (cur->sourceCrop.right - cur->sourceCrop.left);
        win->video_height = (cur->sourceCrop.bottom - cur->sourceCrop.top);
    }

    win->layer_index = layer_idx;
    win->status = HWC_WIN_RESERVED;

    if (!win->set_win_flag)
        return 0;

    ret = v4l2_overlay_stream_off(win);
    if (ret) {
        LOGE("%s: steam off error", __func__);
        goto end;
    } else {
        win->qd_buf_count = 0;
    }

    if (!win->zero_copy) {
        for (unsigned int i = 0; i < win->num_of_buffer; i++) {
            v4l2_overlay_unmap_buf(win->buffers[i], win->buffers_len[i]);
        }
    }

    ret = v4l2_overlay_init(win);
    if (ret) {
        LOGE("%s: Error initializing heo overlay", __func__);
        goto end;
    }

    if (win->set_win_flag == 1) {
        ret = v4l2_overlay_set_position(win);
        if ( ret < 0) {
            LOGE("%s::v4l2_overlay_set_position is failed : %s",
                 __func__, strerror(errno));
        }
        win->set_win_flag = 0;
    }

    if (win->buffers)
        delete [] win->buffers;
    if (win->buffers_len)
        delete [] win->buffers_len;

    win->num_of_buffer = NUM_OF_HEO_BUF;

    ret = v4l2_overlay_req_buf(win);
    if (ret) {
        LOGE("%s: Failed requesting buffers", __func__);
        goto end;
    }

    win->buffers = new void* [win->num_of_buffer];
    win->buffers_len = new size_t [win->num_of_buffer];

    if (!win->buffers || !win->buffers_len) {
        LOGE("%s: Failed alloc'ing buffer arrays", __func__);
        ret = -ENOMEM;
        goto end;
    }

    if (!win->zero_copy) {
        for (unsigned int j = 0; j < win->num_of_buffer; j++) {
            ret = v4l2_overlay_map_buf(win->fd, j, &win->buffers[j], &win->buffers_len[j]);
            if (ret) {
                LOGE("%s: Failed mapping buffers", __func__);
                goto end;
            }
            LOGD("%s:: mapping success, fd:%d, num:%d, buffers:%p, buffers_len:%d",
                 __func__, win->fd, j, win->buffers[j], win->buffers_len[j]);
        }
    }

    ret = v4l2_overlay_stream_on(win);
    if (ret) {
        LOGE("%s: steam on error", __func__);
        goto end;
    }

    LOGD("%s:: win_x %d win_y %d win_w %d win_h %d lay_idx %d win_idx %d iFormat %d",
         __func__, win->rect_info.x, win->rect_info.y, win->rect_info.w,
         win->rect_info.h, win->layer_index, win_idx, prev_handle->iFormat );

    return 0;

end:
    LOGE("%s: ret is %d", __func__, ret);
    return ret;
}

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    int overlay_win_cnt = ctx->num_of_avail_ovl;
    int overlay_win_heo_cnt = ctx->num_of_avail_heo;
    int compositionType = 0;
    int ret;

    //if geometry is not changed, there is no need to do any work here
    if (!list || (!(list->flags & HWC_GEOMETRY_CHANGED)))
        return 0;

    //all the windows are free here....
    for (unsigned int i = 0; i < ctx->num_of_avail_ovl; i++) {
        ctx->win[i].status = HWC_WIN_FREE;
    }

    for (unsigned int i = 0; i < ctx->num_of_avail_heo; i++) {
        ctx->win_heo[i].status = HWC_WIN_FREE;
    }

    ctx->num_of_hwc_layer_prev = ctx->num_of_hwc_layer;
    ctx->num_of_hwc_layer = 0;
    ctx->num_of_fb_layer = 0;

    LOGV("%s:: hwc_prepare list->numHwLayers %d", __func__, list->numHwLayers);

    for (int i = list->numHwLayers -1 ; i >= 0 ; i--) {
        int usage =0;
        hwc_layer_t* cur = &list->hwLayers[i];

        if ((overlay_win_cnt + overlay_win_heo_cnt) > 0 && 
			(list->numHwLayers > 1 || ctx->num_of_hwc_layer_prev > 0)) {
            compositionType = get_hwc_compos_decision(cur, &usage);

            if (compositionType == HWC_FRAMEBUFFER) {
                cur->compositionType = HWC_FRAMEBUFFER;
                ctx->num_of_fb_layer++;
            } else {
                if (usage == USE_OVR) {
                    ret = assign_overlay_window(ctx, cur, overlay_win_cnt -1, i);
                    if (ret != 0) {
                        cur->compositionType = HWC_FRAMEBUFFER;
                        ctx->num_of_fb_layer++;
                        continue;
                    }
                    overlay_win_cnt--;
                } else if (usage == USE_HEO) {
                    ret = assign_heo_overlay_window(ctx, cur, overlay_win_heo_cnt -1, i);
                    if (ret != 0) {
                        cur->compositionType = HWC_FRAMEBUFFER;
                        ctx->num_of_fb_layer++;
                        continue;
                    }
                    overlay_win_heo_cnt--;
                } else {
                    LOGE("%s: unknow usage type %d", __func__, usage);
                    continue;
                }

                cur->compositionType = HWC_OVERLAY;
                cur->hints = HWC_HINT_CLEAR_FB;
                ctx->num_of_hwc_layer++;
            }
        } else {
            cur->compositionType = HWC_FRAMEBUFFER;
            ctx->num_of_fb_layer++;
        }
    }

    if(list->numHwLayers != (ctx->num_of_fb_layer + ctx->num_of_hwc_layer))
        LOGE("%s:: numHwLayers %d num_of_fb_layer %d num_of_hwc_layer %d ",
             __func__, list->numHwLayers, ctx->num_of_fb_layer,
             ctx->num_of_hwc_layer);

    if (overlay_win_cnt > 0) {
        //turn off the free windows
        for (int i = overlay_win_cnt - 1; i >= 0; i--) {
            ctx->win[i].status = HWC_WIN_RELEASE;
        }
    }

    if (overlay_win_heo_cnt > 0) {
        //turn off the free windows
        for (int i = overlay_win_heo_cnt - 1; i >= 0; i--) {
            ctx->win_heo[i].status = HWC_WIN_RELEASE;
        }
    }
    return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
                   hwc_display_t dpy,
                   hwc_surface_t sur,
                   hwc_layer_list_t* list)
{
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    hwc_layer_t* cur;
    struct hwc_win_info_t *win;
    struct hwc_win_info_t_heo *win_heo;
    int ret;
    struct sam_img src_img;
    struct sam_img dst_img;
    struct sam_rect src_rect;
    struct sam_rect dst_rect;


    if (dpy == NULL && sur == NULL && list == NULL) {
        // release our resources, the screen is turning off
        // in our case, there is nothing to do.
        ctx->num_of_fb_layer_prev = 0;
        return 0;
    }

    bool need_swap_buffers = ctx->num_of_fb_layer > 0;

    ctx->num_of_fb_layer_prev = ctx->num_of_fb_layer;

    if (!list) {
        /* turn off the all windows */
        for (unsigned int i = 0; i < ctx->num_of_avail_ovl; i++) {
            reset_win_rect_info(&ctx->win[i]);
            ctx->win[i].status = HWC_WIN_FREE;
        }

        /* turn off the all heo layers */
        for (unsigned int i = 0; i < ctx->num_of_avail_heo; i++) {
            reset_heo_win_rect_info(&ctx->win_heo[i]);
            ctx->win_heo[i].status = HWC_WIN_FREE;
        }
        ctx->num_of_hwc_layer = 0;
        return 0;
    }

    /* copy the content of hardware layers here */
    for (unsigned int i = 0; i < ctx->num_of_avail_ovl; i++) {
        win = &ctx->win[i];
        if (win->status == HWC_WIN_RESERVED) {
            cur = &list->hwLayers[win->layer_index];

            if (cur->compositionType == HWC_OVERLAY) {
                if(copy_src_content(cur, win,i) < 0) {
                    LOGE("%s:: win-id: %d, failed to copy data to overlay frame buffer", __func__, i);
                    continue;
                }

            } else {
                LOGE("%s:: error : layer %d compositionType should have been \
                        HWC_OVERLAY", __func__, win->layer_index);
                win->status = HWC_WIN_RELEASE;
                continue;
            }

            if (win->set_win_flag == 1) {
            /* set the window position with new conf..., don't allow failed */
                if (window_set_pos(win) < 0) {
                    LOGE("Emergency error (%s) ::window_set_pos is failed : %s", __func__,
                                strerror(errno));
                    continue;
                }
                win->set_win_flag = 0;
            }
            
        } else {
            LOGV("%s:: OVR window %d status should have been HWC_WIN_RESERVED \
                     by now... ", __func__, i);
            win->status = HWC_WIN_RELEASE;
        }
    }

    /* copy the content of heo layers here */
    for (unsigned int i = 0; i < ctx->num_of_avail_heo; i++) {
        win_heo = &ctx->win_heo[i];
        if (win_heo->status == HWC_WIN_RESERVED) {
            cur = &list->hwLayers[win_heo->layer_index];
            if (win_heo->set_win_flag == 1) {
                if (v4l2_overlay_set_position(win_heo) < 0) {
                    LOGE("%s::v4l2_overlay_set_position is failed : %s",
                         __func__, strerror(errno));
                    continue;
                }
                win_heo->set_win_flag = 0;
            }

            if (cur->compositionType == HWC_OVERLAY) {
                if(copy_heo_src_content(cur, win_heo,i) < 0) {
                    LOGE("%s:: heo-id: %d, failed to copy data to overlay frame buffer", __func__, i);
                    continue;
                }
            } else {
                LOGE("%s:: error : heo layer %d compositionType should have been \
                        HWC_OVERLAY", __func__, win->layer_index);
                win->status = HWC_WIN_RELEASE;
                continue;
            }
        } else {
            LOGV("%s:: HEO window %d status should have been HWC_WIN_RESERVED \
                     by now... ", __func__, i);
        }
    }

    /* compose the hardware layers here */
    // Base layer
    if (need_swap_buffers || !list) {
        EGLBoolean sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
        if (!sucess) {
            LOGE("%s: eglSwapBuffers failed", __func__);
            return HWC_EGL_ERROR;
        }
    }

    for (unsigned int i = 0; i < ctx->num_of_avail_ovl; i++) {
        win = &ctx->win[i];
        if(win->status == HWC_WIN_RELEASE) {
            reset_win_rect_info(win);
            win->status = HWC_WIN_FREE;
        }
    }

    for (unsigned int i = 0; i < ctx->num_of_avail_heo; i++) {
        win_heo = &ctx->win_heo[i];
        if(win_heo->status == HWC_WIN_RELEASE) {
            reset_heo_win_rect_info(win_heo);
            win_heo->status = HWC_WIN_FREE;
        }
    }

    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    int ret = 0;
    int i;

    if (ctx) {
        for (unsigned i = 0; i < ctx->num_of_avail_ovl; i++) {
            if (window_close(&ctx->win[i]) < 0) {
                LOGE("%s::window_close() fail", __func__);
                ret = -1;
            }
        }

        for (unsigned i=0; i < ctx->num_of_avail_heo; i++) {
            if (v4l2_overlay_close(&ctx->win_heo[i]) < 0) {
                LOGE("%s::v4l2_overlay_close() fail", __func__);
                ret = -1;
            }
        }
        free(ctx);
    }
    return 0;
}

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device)
{
    int status = 0;
    int NUM_OF_WIN = 0, NUM_OF_HEO_WIN = 0;
    DIR*    dir;
    struct dirent* de;
    struct hwc_win_info_t *win;
    struct hwc_win_info_t_heo *win_heo;

    if (strcmp(name, HWC_HARDWARE_COMPOSER))
        return -EINVAL;

    struct hwc_context_t *dev;
    dev = (hwc_context_t*)malloc(sizeof(*dev));

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;

    dev->device.prepare = hwc_prepare;
    dev->device.set = hwc_set;

    *device = &dev->device.common;

    if (!(dir = opendir("/dev/graphics"))) {
        LOGE("opendir failed (%s)", strerror(errno));
    }

    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, "fb0"))
            continue;
        NUM_OF_WIN++;
        LOGD("We find %d'th ovr layer: %s", NUM_OF_WIN, de->d_name);
    }
    closedir(dir);

    /* determine the number of available overlay layers, default is 2 */
    char value[PROPERTY_VALUE_MAX];
    if (property_get("ro.hwc.ovl_num", value, "2") > 0) {
        dev->num_of_avail_ovl = SAM_MIN(atoi(value), NUM_OF_WIN);
    } else {
        dev->num_of_avail_ovl = NUM_OF_WIN;
    }

    /* open Ovrlayer here */
    for (unsigned int i = 0; i < dev->num_of_avail_ovl; i++) {
        if (window_open(&(dev->win[i]), i) < 0) {
            LOGE("%s:: Failed to open window %d device ", __func__, i);
            dev->num_of_avail_ovl--;
        }
    }

    /* open HEOlayer here */
    if (!(dir = opendir("/sys/class/video4linux"))) {
        LOGE("opendir failed (%s)", strerror(errno));
    }

    while ((de = readdir(dir))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..") || !strcmp(de->d_name, "fb0"))
            continue;
        if (v4l2_overlay_open(&(dev->win_heo[NUM_OF_HEO_WIN]), de->d_name) < 0)
            continue;
        NUM_OF_HEO_WIN++;
        LOGD("We find %d'th heo layer: %s", dev->num_of_avail_heo, de->d_name);
    }
    closedir(dir);

    if (property_get("ro.hwc.ovl_heo_num", value, "1") > 0) {
        dev->num_of_avail_heo = SAM_MIN(NUM_OF_HEO_WIN, atoi(value));
    } else {
        dev->num_of_avail_heo = NUM_OF_HEO_WIN;
    }

    for (unsigned int i = 0; i < NUM_OF_HEO_WIN - dev->num_of_avail_heo; i++) {
        if (v4l2_overlay_close(&(dev->win_heo[dev->num_of_avail_heo + i])) == 0) {
            LOGV("%s:: Closing heo layer %d device ", __func__, i);
        }
    }

    /* get default window config */
    if (window_get_global_lcd_info(&dev->lcd_info) < 0) {
        LOGE("%s::window_get_global_lcd_info is failed : %s",
             __func__, strerror(errno));
        status = -EINVAL;
        goto err;
    }

    // 2 is the numbers of buffers for page flipping, defined in framebuffer.cpp
    dev->lcd_info.yres_virtual = dev->lcd_info.yres * 2;

    /* initialize the window context */
    for (unsigned int i = 0; i < dev->num_of_avail_ovl; i++) {
        win = &dev->win[i];
        memcpy(&win->lcd_info, &dev->lcd_info, sizeof(struct fb_var_screeninfo));

        if (window_get_var_info(win) < 0) {
            LOGE("%s::window_get_info is failed : %s",
                 __func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        win->var_info.yres_virtual = win->var_info.yres * NUM_OF_WIN_BUF;

        win->var_info.bits_per_pixel = 32;/* MAX for RGBA8888 */
        win->transp_offset = 24; /* A[31:24] */

         if (window_reset_pos(win) < 0) {
            LOGE("%s::window_reset_pos is failed : %s",
					__func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        if (window_get_fix_info(win) < 0) {
            LOGE("%s::window_get_info is failed : %s",
                 __func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        win->size = win->fix_info.line_length * win->var_info.yres;

        LOGD("line_length: %d, yres: %d, win->size is %d",win->fix_info.line_length, win->var_info.yres, win->size);

        if (!win->fix_info.smem_start) {
            LOGE("%s:: win-%d failed to get the reserved memory", __func__, i);
            status = -EINVAL;
            goto err;
        }

        for (int j = 0; j < NUM_OF_WIN_BUF; j++) {
            win->phy_addr[j] = win->fix_info.smem_start + (win->size * j);
            LOGI("%s::win-%d phy_add[%d] %x ", __func__, i, j, win->phy_addr[j]);
        }

        if (window_mmap(win) < 0) {
            LOGE("%s::window_mmap is failed : %s",
                 __func__, strerror(errno));
            status = -EINVAL;
            goto err;
        }

        window_show_var_info(win);
         
    }

    for (unsigned int i = 0; i < dev->num_of_avail_heo; i++) {
        win_heo = &dev->win_heo[i];
        memcpy(&win_heo->lcd_info, &dev->lcd_info, sizeof(struct fb_var_screeninfo));
    }

    LOGD("%s:: success\n", __func__);
    return 0;

err:
    for (unsigned int i = 0; i < dev->num_of_avail_ovl; i++) {
        if (window_close(&dev->win[i]) < 0)
            LOGE("%s::window_close() fail", __func__);
    }

    for (unsigned int i = 0; i < dev->num_of_avail_heo; i++) {
        if (v4l2_overlay_close(&dev->win_heo[i]) < 0)
            LOGE("%s::window_close() fail", __func__);
    }
    return status;
}
