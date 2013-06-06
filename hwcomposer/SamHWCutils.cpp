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

/*
 *
 * @author Xin, Liu(liuxing@embedinfo.com)
 * @date   2012-07-28
 *
 */

 #include "common.h"

 int window_open(struct hwc_win_info_t *win, int id)
{
    char name[64];

    char const * const device_template = "/dev/graphics/fb%u";
    /* window & FB & ovrlayer maping
        win-id:0      -> fb1 -> ovrlayer 1
        win-id:1      -> fb2 -> ovrlayer2
        NUM_OF_FB -> fb0 -> baselayer
    */

    if( id < 0 || id > NUM_OF_FB - 1 ) {
        ALOGE("%s::id(%d) is weird", __func__, id);
        goto error;
    }

    snprintf(name, 64, device_template, (id + 1)%NUM_OF_FB );

    win->fd = open(name, O_RDWR);
    if (win->fd < 0) {
		ALOGE("%s::Failed to open window device (%s) : %s",
				__func__, strerror(errno), name);
        goto error;
    }

    win->buf_index = NUM_OF_WIN_BUF -1;

    ALOGD("%s, open fd:%d, id:%d", __func__, win->fd, id);

    return 0;

error:
    if (0 <= win->fd)
        close(win->fd);
    win->fd = -1;

    return -1;
}

int window_close(struct hwc_win_info_t *win)
{
    int ret = 0;
    ALOGD("%s, close fd %d", __func__, win->fd);
    if (0 <= win->fd){
        window_munmap(win);
        ret = close(win->fd);
    }    
    win->fd = -1;

    return ret;
}

int window_get_fix_info(struct hwc_win_info_t *win)
{
    if (ioctl(win->fd, FBIOGET_FSCREENINFO, &win->fix_info) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed : %s", strerror(errno));
        goto error;
    }

    return 0;

error:
    win->fix_info.smem_start = 0;

    return -1;
}

int window_get_var_info(struct hwc_win_info_t *win)
{
    if (ioctl(win->fd, FBIOGET_VSCREENINFO, &win->var_info) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed : %s", strerror(errno));
        goto error;
    }

    return 0;

error:
    return -1;
}

#define PUT(string,value) ALOGD("%20s : %d", string, win->var_info.value)

int window_show_var_info(struct hwc_win_info_t *win)
{
    PUT("xres", xres);
    PUT("yres", yres);
    PUT("xres_virtual", xres_virtual);
    PUT("yres_virtual", yres_virtual);
    PUT("bits_per_pixel", bits_per_pixel);
    return 0;
}

int window_set_var_info(struct hwc_win_info_t *win)
{
    if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &win->var_info) < 0) {
        ALOGE("FBIOGET_FSCREENINFO failed : %s", strerror(errno));
        return -1;
    }

    return 0;
}

int window_set_pos(struct hwc_win_info_t *win)
{
    int xpos = -1, ypos = -1;

    xpos = win->rect_info.x;
    ypos = win->rect_info.y;
    win->var_info.xres = win->rect_info.w;
    win->var_info.yres = win->rect_info.h;

    win->var_info.yoffset = win->lcd_info.yres * win->buf_index;

    win->var_info.activate &= ~FB_ACTIVATE_MASK;
    win->var_info.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    win->var_info.nonstd = xpos << 10 | ypos;
    if(win->transp_offset)
        win->var_info.accel_flags = 1;
    else
        win->var_info.accel_flags = 0;

    win->var_info.nonstd |= 1 << 31;
    if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &(win->var_info)) < 0) {
        ALOGE("%s::FBIOPUT_VSCREENINFO(fd:%d, w:%d, h:%d) fail",
          		__func__, win->fd, win->rect_info.w, win->rect_info.h);
        return -1;
    }    
    return 0;
}

int window_reset_pos(struct hwc_win_info_t *win)
{
    win->var_info.nonstd = 0;

    win->var_info.yoffset = win->lcd_info.yres * win->buf_index;

    win->var_info.activate &= ~FB_ACTIVATE_MASK;
    win->var_info.activate |= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

    if(win->transp_offset)
        win->var_info.accel_flags = 1;
    else
        win->var_info.accel_flags = 0;

    if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &(win->var_info)) < 0) {
        ALOGE("%s::FBIOPUT_VSCREENINFO(fd:%d, w:%d, h:%d) fail",
          		__func__, win->fd, win->rect_info.w, win->rect_info.h);
        return -1;
    }
    return 0;
}

int window_mmap(struct hwc_win_info_t *win)
{
    size_t fbSize = roundUpToPageSize(win->size * NUM_OF_WIN_BUF);
    win->base = mmap(0, fbSize, PROT_READ|PROT_WRITE, MAP_SHARED, win->fd, 0);
    
    if (win->base == MAP_FAILED) {
        ALOGE("Error mapping the framebuffer (%s)", strerror(errno));
        return -errno;
    }

    for (int k = 0; k < NUM_OF_WIN_BUF; k++) {
        win->vir_addr[k] = intptr_t(win->base) + (win->size * k);
        ALOGD("%s::win vir_add[%d] 0x%p", __func__, k, win->vir_addr[k]);
    }

    return 0;
}

int window_munmap(struct hwc_win_info_t *win)
{
    if(win->base != NULL){
        munmap (win->base, win->size * NUM_OF_WIN_BUF);
        win->base = NULL;
        for (int k = 0; k < NUM_OF_WIN_BUF; k++) {
            win->vir_addr[k] = intptr_t(NULL);
        }
    }
    
    return 0;
}

int window_pan_display(struct hwc_win_info_t *win)
{
    struct fb_var_screeninfo *lcd_info = &(win->lcd_info);

    lcd_info->yoffset = lcd_info->yres * win->buf_index;

    if (ioctl(win->fd, FBIOPAN_DISPLAY, lcd_info) < 0) {
        ALOGE("%s::FBIOPAN_DISPLAY(%d / %d / %d) fail(%s)",
            	__func__, lcd_info->yres, win->buf_index, lcd_info->yres_virtual,
            strerror(errno));
        return -1;
    }
    return 0;
}

int window_show(struct hwc_win_info_t *win)
{
    win->var_info.nonstd |= 1 << 31;

    if(win->power_state == 0) {
    	if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &(win->var_info)) < 0) {
        	ALOGE("%s::FBIOPUT_VSCREENINFO(fd:%d, w:%d, h:%d) fail",
          		__func__, win->fd, win->rect_info.w, win->rect_info.h);
       		return -1;
    	}
        win->power_state = 1;
    }
    return 0;
}

int window_hide(struct hwc_win_info_t *win)
{
    win->var_info.nonstd = 0;
    if (win->power_state == 1) {
    	/*if (ioctl(win->fd, FBIOPUT_VSCREENINFO, &(win->var_info)) < 0) {
        	ALOGE("%s::FBIOPUT_VSCREENINFO(fd:%d, w:%d, h:%d) fail",
          		__func__, win->fd, win->rect_info.w, win->rect_info.h);
       		return -1;
    	}*/
        win->power_state = 0;
    }
    return 0;
}

int window_get_global_lcd_info(struct fb_var_screeninfo *lcd_info)
{
    struct hwc_win_info_t win;
    int ret = 0;

    if (window_open(&win, NUM_OF_FB - 1)  < 0) {
        ALOGE("%s:: Failed to open baselayer device ", __func__);
        return -1;
    }

    if (ioctl(win.fd, FBIOGET_VSCREENINFO, lcd_info) < 0) {
        ALOGE("FBIOGET_VSCREENINFO failed : %s", strerror(errno));
        ret = -1;
        goto fun_err;
    }

fun_err:
    if (window_close(&win) < 0)
        ALOGE("%s::baselayer close fail", __func__);   

    return ret;
}

int window_global_lcd_event_control(struct hwc_context_t *ctx, int enabled)
{
    int ret = 0;
    int val = !!enabled;

    if(ctx->base_lcd_fb <= 0) {
        struct hwc_win_info_t win;
        if (window_open(&win, NUM_OF_FB - 1)  < 0) {
            ALOGE("%s:: Failed to open baselayer device ", __func__);
            return -1;
        }
        ctx->base_lcd_fb = win.fd;
    }

    if (ioctl(ctx->base_lcd_fb, ATMEL_LCDFB_SET_VSYNC_INT, &val) < 0) {
        ALOGE("ATMEL_LCDFB_SET_VSYNC_INT failed : %s", strerror(errno));
        ret = -1;
        goto fun_err;
    }

    return 0;

fun_err:
    if (close(ctx->base_lcd_fb) < 0)
        ALOGE("%s::baselayer close fail", __func__);
    else
        ctx->base_lcd_fb = -1;
    return ret;
}
