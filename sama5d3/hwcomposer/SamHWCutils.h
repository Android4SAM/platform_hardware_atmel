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
#ifndef ANDROID_SAM_HWC_UTILS_H_
#define ANDROID_SAM_HWC_UTILS_H_

#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>

enum {
    HWC_WIN_FREE = 0,
    HWC_WIN_RESERVED,
    HWC_WIN_RELEASE,
};

inline int SAM_MIN(int x, int y) {
    return ((x < y) ? x : y);
}

inline int SAM_MAX(int x, int y) {
    return ((x > y) ? x : y);
}

inline uint32_t roundUpToPageSize(uint32_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}


struct hwc_win_info_t {
    int        fd;
    int        size;
    struct sam_rect   rect_info;
    uint32_t transp_offset;
    uint32_t   phy_addr[NUM_OF_WIN_BUF];
    uint32_t   vir_addr[NUM_OF_WIN_BUF];
    int        buf_index;
    int        power_state;
    int        blending;
    int        layer_index;
    uint32_t   layer_prev_buf;
    int        set_win_flag;
    int        status;
    int        vsync;
    void*    base;
    uint32_t layer_prev_format;

    struct fb_fix_screeninfo fix_info;
    struct fb_var_screeninfo var_info;
    struct fb_var_screeninfo lcd_info;
};

int window_open(struct hwc_win_info_t *win, int id);
int window_close(struct hwc_win_info_t *win);
int window_set_pos(struct hwc_win_info_t *win);
int window_reset_pos(struct hwc_win_info_t *win);
int window_get_fix_info(struct hwc_win_info_t *win);
int window_get_var_info(struct hwc_win_info_t *win);
int window_set_var_info(struct hwc_win_info_t *win);
int window_show_var_info(struct hwc_win_info_t *win);
int window_show(struct hwc_win_info_t *win);
int window_hide(struct hwc_win_info_t *win);
int window_mmap(struct hwc_win_info_t *win);
int window_munmap(struct hwc_win_info_t *win);
int window_pan_display(struct hwc_win_info_t *win);
int window_get_global_lcd_info(struct fb_var_screeninfo *lcd_info);


#endif /* ANDROID_SAM_HWC_UTILS_H_*/
