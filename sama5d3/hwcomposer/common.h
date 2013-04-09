#ifndef ANDROID_SAM_COMMON_H_
#define ANDROID_SAM_COMMON_H_

#include <fcntl.h>
#include <errno.h>
#include <cutils/log.h>
#include <cutils/atomic.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "linux/fb.h"
#include <linux/videodev.h>
#include <asm/page.h>			// just want PAGE_SIZE define

#define NUM_OF_FB             (3)
#define MAX_NUM_OF_WIN (NUM_OF_FB - 1) //Plus 1, it is fb0
#define NUM_OF_WIN_BUF      (2)

#define MAX_NUM_OF_HEO (1)
#define NUM_OF_HEO_BUF (3)

struct sam_rect {
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
};

struct sam_img {
    uint32_t w;
    uint32_t h;
    uint32_t format;
    uint32_t base;
    uint32_t offset;
    int      mem_id;
    int      mem_type;
};

enum {
    USE_OVR = 1,
    USE_HEO,
};

#include "SamHWCutils.h"
#include "v4l2_utils.h"

struct hwc_context_t {
    hwc_composer_device_t     device;

    /* our private state goes below here */
    struct hwc_win_info_t     win[MAX_NUM_OF_WIN];
    struct hwc_win_info_t_heo     win_heo[MAX_NUM_OF_HEO];
    struct fb_var_screeninfo  lcd_info;
    unsigned int              num_of_avail_ovl;
    unsigned int		num_of_avail_heo;
    unsigned int              num_of_fb_layer;
    unsigned int              num_of_hwc_layer;
    unsigned int              num_of_hwc_layer_prev;
    unsigned int              num_of_fb_layer_prev;
};


#endif /* ANDROID_SAM_COMMON_H_*/
