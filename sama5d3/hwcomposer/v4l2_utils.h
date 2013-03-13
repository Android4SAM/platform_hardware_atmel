/*
 * Copyright 2009 Google Inc. All Rights Reserved.
 * Author: rschultz@google.com (Rebecca Schultz Zavin)
 */

#ifndef ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_
#define ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_

struct hwc_win_info_t_heo {
    int        fd;
    int        size;
    struct sam_rect   rect_info;
    uint32_t video_width;
    uint32_t video_height;
    uint32_t transp_offset;
    void       **buffers;
    size_t     *buffers_len;
    uint32_t   num_of_buffer;
    uint32_t   qd_buf_count;
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

    bool      zero_copy;
    bool      steamEn;

    struct fb_fix_screeninfo fix_info;
    struct fb_var_screeninfo lcd_info;
};


int v4l2_overlay_querycap(int fd);
int v4l2_overlay_open(struct hwc_win_info_t_heo *win, const char *dir);
int v4l2_overlay_close(struct hwc_win_info_t_heo *win);
int v4l2_overlay_get_caps(int fd, struct v4l2_capability *caps);
int v4l2_overlay_req_buf(struct hwc_win_info_t_heo *win);
int v4l2_overlay_query_buffer(int fd, int index, struct v4l2_buffer *buf);
int v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len);
int v4l2_overlay_unmap_buf(void *start, size_t len);
int v4l2_overlay_stream_on(struct hwc_win_info_t_heo *win);
int v4l2_overlay_stream_off(struct hwc_win_info_t_heo *win);
int v4l2_overlay_q_buf(int fd, int index, int zerocopy);
int v4l2_overlay_dq_buf(int fd, int *index, int zerocopy);
int v4l2_overlay_init(struct hwc_win_info_t_heo *win);
int v4l2_overlay_get_input_size(int fd, uint32_t *w, uint32_t *h,
                                uint32_t *fmt);
int v4l2_overlay_set_position(struct hwc_win_info_t_heo *win);
int v4l2_overlay_get_position(int fd, int32_t *x, int32_t *y, int32_t *w,
                              int32_t *h);
int v4l2_overlay_set_flip(int fd, int degree);
int v4l2_overlay_set_rotation(int fd, int degree, int step);

enum {
    V4L2_OVERLAY_PLANE_GRAPHICS,
    V4L2_OVERLAY_PLANE_VIDEO1,
    V4L2_OVERLAY_PLANE_VIDEO2,
};

enum {
    /* support customed format for zero copy */
    HAL_PIXEL_FORMAT_YCbCr_420_SP = 0x21,
    HAL_PIXEL_FORMAT_YCbCr_420_P = 0x13,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP = 0x100,
    HAL_PIXEL_FORMAT_CUSTOM_YCbCr_422_I = 0x101,
    HAL_PIXEL_FORMAT_CUSTOM_CbYCrY_422_I = 0x102,
    HAL_PIXEL_FORMAT_CUSTOM_YCrCb_420_SP = 0x103,
    HAL_PIXEL_FORMAT_CUSTOM_MAX
};

enum {
    PFT_RGB,
    PFT_YUV420,
    PFT_YUV422,
    PFT_YUV444,
};

struct mapping_data {
    int fd;
    size_t length;
    uint32_t offset;
    void *ptr;
};

typedef unsigned int dma_addr_t;
struct fimc_buf {
    dma_addr_t	base[3];
    size_t		length[3];
};

#define ALL_BUFFERS_FLUSHED -66

#endif  /* ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_*/
