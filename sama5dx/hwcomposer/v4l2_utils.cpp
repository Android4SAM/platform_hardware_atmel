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

/* #define OVERLAY_DEBUG 1 */
#undef  LOG_TAG
#define LOG_TAG "v4l2_utils"

#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <cutils/log.h>
#include <linux/videodev.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "common.h"

#define LOG_FUNCTION_NAME    ALOGV("%s: %s",  __FILE__, __func__);
//#define ALOG_FUNCTION_NAME    ALOGD("%s", __func__);

#define V4L2_CID_PRIV_OFFSET         0x0
#define V4L2_CID_PRIV_ROTATION       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 0)
#define V4L2_CID_PRIV_COLORKEY       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 1)
#define V4L2_CID_PRIV_COLORKEY_EN    (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 2)

int v4l2_overlay_open(struct hwc_win_info_t_heo *win, const char *dir)
{
    LOG_FUNCTION_NAME

    char name[64];
    char const * const device_template = "/dev/%s";

    snprintf(name, 64, device_template, dir);

    win->fd = open(name, O_RDWR);
    if (win->fd < 0) {
        ALOGE("%s::Failed to open window device (%s) : %s",
				__func__, strerror(errno), name);
        goto error;
    }

    if (v4l2_overlay_querycap(win->fd) < 0) {
        ALOGD("%s:: we can not use %s", __func__, name);
        goto error;
    }

    /* Reserve: if the video buffer address could used by DMA
      * Then, win->zero_copy = true;
      * Then we can reduce on copy
      */
    win->zero_copy = false;

    ALOGD("%s, open %s successful: fd:%d", __func__, name, win->fd);

    return 0;

error:
    if (0 <= win->fd)
        close(win->fd);
    win->fd = -1;
    
    return -1;

}

int v4l2_overlay_close(struct hwc_win_info_t_heo *win)
{
    int ret = 0;
    ALOGD("%s, close fd %d", __func__, win->fd);
    if (0 <= win->fd){
        /* ummap */
        if (!win->zero_copy) {
            for (unsigned int i = 0; i < win->num_of_buffer; i++) {
                v4l2_overlay_unmap_buf(win->buffers[i], win->buffers_len[i]);
            }
        }
        /* request 0 buffer */
        win->num_of_buffer = 0;
        if (v4l2_overlay_req_buf(win) < 0) {
            ALOGE("%s:Failed requesting buffers\n", __func__);
        }

        /* close */
        ret = close(win->fd);
    }
    win->fd = -1;

    if(win->buffers)
        delete [] win->buffers;
    if(win->buffers_len)
        delete [] win->buffers_len;      

    return ret;
}

void dump_pixfmt(struct v4l2_pix_format *pix)
{
    ALOGV("w: %d\n", pix->width);
    ALOGV("h: %d\n", pix->height);
    ALOGV("color: %x\n", pix->colorspace);

    switch (pix->pixelformat) {
    case V4L2_PIX_FMT_YUYV:
        ALOGV("YUYV\n");
        break;
    case V4L2_PIX_FMT_UYVY:
        ALOGV("UYVY\n");
        break;
    case V4L2_PIX_FMT_RGB565:
        ALOGV("RGB565\n");
        break;
    case V4L2_PIX_FMT_RGB565X:
        ALOGV("RGB565X\n");
        break;
    default:
        ALOGV("not supported\n");
    }
}

void dump_crop(struct v4l2_crop *crop)
{
    ALOGV("crop l: %d ", crop->c.left);
    ALOGV("crop t: %d ", crop->c.top);
    ALOGV("crop w: %d ", crop->c.width);
    ALOGV("crop h: %d\n", crop->c.height);
}

void dump_window(struct v4l2_window *win)
{
    ALOGV("window l: %d ", win->w.left);
    ALOGV("window t: %d ", win->w.top);
    ALOGV("window w: %d ", win->w.width);
    ALOGV("window h: %d\n", win->w.height);
}

void v4l2_overlay_dump_state(int fd)
{
    struct v4l2_format format;
    struct v4l2_crop crop;
    int ret;

    ALOGV("dumping driver state:");
    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if (ret < 0)
        return;
    ALOGV("output pixfmt:\n");
    dump_pixfmt(&format.fmt.pix);

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if (ret < 0)
        return;
    ALOGV("v4l2_overlay window:\n");
    dump_window(&format.fmt.win);

    crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = ioctl(fd, VIDIOC_G_CROP, &crop);
    if (ret < 0)
        return;
    ALOGV("output crop:\n");
    dump_crop(&crop);
}

static void error(int fd, const char *msg)
{
    ALOGE("Error = %s from %s", strerror(errno), msg);
#ifdef OVERLAY_DEBUG
    v4l2_overlay_dump_state(fd);
#endif
}

static int v4l2_overlay_ioctl(int fd, int req, void *arg, const char* msg)
{
    int ret = 0;
    ret = ioctl(fd, req, arg);
    if (ret < 0) {
        error(fd, msg);
        return -1;
    }
    return 0;
}

#define v4l2_fourcc(a, b, c, d)  ((__u32)(a) | ((__u32)(b) << 8) | \
                                 ((__u32)(c) << 16) | ((__u32)(d) << 24))
/* 12  Y/CbCr 4:2:0 64x32 macroblocks */
#define V4L2_PIX_FMT_NV12T       v4l2_fourcc('T', 'V', '1', '2')

int configure_pixfmt(struct v4l2_pix_format *pix, int32_t fmt,
        uint32_t w, uint32_t h)
{
    LOG_FUNCTION_NAME
    int fd;

    switch (fmt) {
    case HAL_PIXEL_FORMAT_YV12:
        pix->pixelformat = V4L2_PIX_FMT_YUV420;
        break;
	case HAL_PIXEL_FORMAT_YCbCr_422_I:
		pix->pixelformat = V4L2_PIX_FMT_YUYV;
		break;
    default:
        ALOGE("%s: unknow format %d", __func__, fmt);
        return -1;
    }
    pix->width = w;
    pix->height = h;
    return 0;
}

static void configure_window(struct v4l2_window *win, int32_t w,
        int32_t h, int32_t x, int32_t y)
{
    LOG_FUNCTION_NAME

    win->w.left = x;
    win->w.top = y;
    win->w.width = w;
    win->w.height = h;
}

void get_window(struct v4l2_format *format, int32_t *x,
        int32_t *y, int32_t *w, int32_t *h)
{
    LOG_FUNCTION_NAME

    *x = format->fmt.win.w.left;
    *y = format->fmt.win.w.top;
    *w = format->fmt.win.w.width;
    *h = format->fmt.win.w.height;
}

int v4l2_overlay_querycap(int fd)
{
    LOG_FUNCTION_NAME
    struct v4l2_capability cap;
    const char *p;
    int ret = 0;

    ret = ioctl(fd, VIDIOC_QUERYCAP, &cap);

    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __func__);
        return -1;
    }
    p = (const char*)cap.card;
    if (strcmp(p, "Atmel HEO Layer")) {
        ALOGD("Not a heo device\n");
        return -1;
    }

    return ret;
}

int v4l2_overlay_init(struct hwc_win_info_t_heo *win)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    struct v4l2_framebuffer fbuf;
    int ret = 0;
    int fd = win->fd;
    int w = win->video_width;
    int h = win->video_height;
    int fmt = win->layer_prev_format;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format, "get format");
    if (ret)
        return ret;
    ALOGV("v4l2_overlay_init:: w=%d h=%d\n", format.fmt.pix.width,
         format.fmt.pix.height);

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    configure_pixfmt(&format.fmt.pix, fmt, w, h);
    ALOGV("v4l2_overlay_init:: w=%d h=%d\n", format.fmt.pix.width,
         format.fmt.pix.height);
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format, "set output format");

    return ret;
}

int v4l2_overlay_get_input_size_and_format(int fd, uint32_t *w, uint32_t *h
                                                 , uint32_t *fmt)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    int ret = 0;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format, "get format");
    *w = format.fmt.pix.width;
    *h = format.fmt.pix.height;
    if (format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        *fmt = V4L2_PIX_FMT_YUYV; //TODO: Dold one is OVERLAY_FORMAT_CbYCrY_422_I
    else
        return -EINVAL;
    return ret;
}

int v4l2_overlay_set_position(struct hwc_win_info_t_heo *win)
{
    LOG_FUNCTION_NAME

    struct v4l2_format format;
    int ret = 0;
    int fd = win->fd;
    int rot_x = win->rect_info.x, rot_y = win->rect_info.y;
    int rot_w = win->rect_info.w, rot_h = win->rect_info.h;

    /* configure the src format pix */
    /* configure the dst v4l2_overlay window */
    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format,
            "get v4l2_overlay format");
    if (ret)
        return ret;
    ALOGV("v4l2_overlay_set_position:: w=%d h=%d", format.fmt.win.w.width
                                                , format.fmt.win.w.height);

    configure_window(&format.fmt.win, rot_w, rot_h, rot_x, rot_y);

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format,
            "set v4l2_overlay format");

    ALOGV("v4l2_overlay_set_position:: w=%d h=%d rotation=%d"
                 , format.fmt.win.w.width, format.fmt.win.w.height, rotation);

    if (ret)
        return ret;
    v4l2_overlay_dump_state(fd);

    return 0;
}

int v4l2_overlay_get_position(int fd, int32_t *x, int32_t *y, int32_t *w,
                              int32_t *h)
{
    struct v4l2_format format;
    int ret = 0;

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format,
                                 "get v4l2_overlay format");

    if (ret)
        return ret;

    get_window(&format, x, y, w, h);

    return 0;
}

int v4l2_overlay_set_flip(int fd, int flip)
{
    LOG_FUNCTION_NAME

	return 0;
}

int v4l2_overlay_set_rotation(int fd, int degree, int step)
{
    LOG_FUNCTION_NAME

	return 0;
}

int v4l2_overlay_req_buf(struct hwc_win_info_t_heo *win)
{
    struct v4l2_requestbuffers reqbuf;
    int ret = 0;

    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (win->zero_copy)
        reqbuf.memory = V4L2_MEMORY_USERPTR;
    else
        reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = win->num_of_buffer;

    ret = ioctl(win->fd, VIDIOC_REQBUFS, &reqbuf);
    if (ret) {
        error(win->fd, "reqbuf ioctl");
		ALOGE("VIDIOC_REQBUFS ERROR");
        return ret;
    }

    /* Note: need more considertion
      * We use dynamic buffer allocate, so we may need to remove this limitaiton
      */
    if (reqbuf.count > win->num_of_buffer) {
        error(win->fd, "Not enough buffer structs passed to get_buffers");
        return -ENOMEM;
    }
	
    if ((reqbuf.count == 0) && (reqbuf.count != win->num_of_buffer)) {
        ALOGE("request buffer error, get 0 buffer");		
        return -ENOSPC;
    }
    win->num_of_buffer = reqbuf.count;

    return 0;
}

static int is_mmaped(struct v4l2_buffer *buf)
{
    return buf->flags == V4L2_BUF_FLAG_MAPPED;
}

static int is_queued(struct v4l2_buffer *buf)
{
    /* is either on the input or output queue in the kernel */
    return (buf->flags & V4L2_BUF_FLAG_QUEUED) ||
        (buf->flags & V4L2_BUF_FLAG_DONE);
}

static int is_dequeued(struct v4l2_buffer *buf)
{
    /* is on neither input or output queue in kernel */
    return !(buf->flags & V4L2_BUF_FLAG_QUEUED) &&
            !(buf->flags & V4L2_BUF_FLAG_DONE);
}

int v4l2_overlay_query_buffer(int fd, int index, struct v4l2_buffer *buf)
{
    LOG_FUNCTION_NAME

    memset(buf, 0, sizeof(struct v4l2_buffer));

    buf->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf->memory = V4L2_MEMORY_MMAP;
    buf->index = index;
    ALOGV("query buffer, mem=%u type=%u index=%u\n",
         buf->memory, buf->type, buf->index);
    return v4l2_overlay_ioctl(fd, VIDIOC_QUERYBUF, buf, "querybuf ioctl");
}

int v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len)
{
    LOG_FUNCTION_NAME

    struct v4l2_buffer buf;
    int ret = 0;

    ret = v4l2_overlay_query_buffer(fd, index, &buf);
    if (ret)
        return ret;

    if (is_mmaped(&buf)) {
        ALOGE("Trying to mmap buffers that are already mapped!\n");
        return -EINVAL;
    }

    *len = buf.length;
    *start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
            fd, buf.m.offset);
    if (*start == MAP_FAILED) {
        ALOGE("map failed, length=%u offset=%u\n", buf.length, buf.m.offset);
        return -EINVAL;
    }
    return 0;
}

int v4l2_overlay_unmap_buf(void *start, size_t len)
{
    LOG_FUNCTION_NAME
    return munmap(start, len);
}

int v4l2_overlay_stream_on(struct hwc_win_info_t_heo *win)
{
    int ret = 0;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (!win->steamEn) {
        ret = v4l2_overlay_ioctl(win->fd, VIDIOC_STREAMON, &type, "stream on");
        if (ret) {
            ALOGE("%s: Stream on Failed!/%d", __func__, ret);
        } else {
            win->steamEn = true;
        }
    } else {
        ALOGV("%s: stream has already on");
    }    

    return ret;
}

int v4l2_overlay_stream_off(struct hwc_win_info_t_heo *win)
{
    int ret = 0;;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (win->steamEn) {
        ret = v4l2_overlay_ioctl(win->fd, VIDIOC_STREAMOFF, &type, "stream off");
        if (ret) {
            ALOGE("%s: Stream Off Failed!/%d", __func__, ret);
        } else {
            win->steamEn = false;
        }
    } else {
        ALOGV("%s: stream has already off");
    }

    return ret;
}

int v4l2_overlay_q_buf(int fd, int buffer, int zerocopy)
{
    struct v4l2_buffer buf;
    int ret = 0;;

    if (zerocopy) {
        /* TODO: the following code need to re-consider, we need a dma addr for softwarerender*/
        uint8_t *pPhyYAddr;
        uint8_t *pPhyCAddr;
        struct fimc_buf fimc_src_buf;
        uint8_t index;

        memcpy(&pPhyYAddr, (void *) buffer, sizeof(pPhyYAddr));
        memcpy(&pPhyCAddr, (void *) (buffer + sizeof(pPhyYAddr)),
               sizeof(pPhyCAddr));
        memcpy(&index,
               (void *) (buffer + sizeof(pPhyYAddr) + sizeof(pPhyCAddr)),
               sizeof(index));

        fimc_src_buf.base[0] = (dma_addr_t) pPhyYAddr;
        fimc_src_buf.base[1] = (dma_addr_t) pPhyCAddr;
        fimc_src_buf.base[2] =
               (dma_addr_t) (pPhyCAddr + (pPhyCAddr - pPhyYAddr)/4);

        buf.index = index;
        buf.memory      = V4L2_MEMORY_USERPTR;
        buf.m.userptr   = (unsigned long)&fimc_src_buf;
        buf.length      = 0;
    } else {
        buf.index = buffer;
        buf.memory      = V4L2_MEMORY_MMAP;
    }

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    buf.flags = 0;

    return v4l2_overlay_ioctl(fd, VIDIOC_QBUF, &buf, "qbuf");
}

int v4l2_overlay_dq_buf(int fd, int *index, int zerocopy)
{
    struct v4l2_buffer buf;
    int ret = 0;;

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    if (zerocopy)
        buf.memory = V4L2_MEMORY_USERPTR;
    else
        buf.memory = V4L2_MEMORY_MMAP;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_DQBUF, &buf, "dqbuf");
    if (ret)
        return ret;
    *index = buf.index;
    return 0;
}
