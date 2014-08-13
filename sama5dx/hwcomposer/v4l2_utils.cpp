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
#include <system/graphics.h>

#define LOG_FUNCTION_NAME    ALOGV("%s: %s",  __FILE__, __func__);
//#define ALOG_FUNCTION_NAME    ALOGD("%s", __func__);

#define V4L2_CID_PRIV_OFFSET         0x0
#define V4L2_CID_PRIV_ROTATION       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 0)
#define V4L2_CID_PRIV_COLORKEY       (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 1)
#define V4L2_CID_PRIV_COLORKEY_EN    (V4L2_CID_PRIVATE_BASE \
        + V4L2_CID_PRIV_OFFSET + 2)

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

int __v4l2_overlay_querycap(int fd, char *cardname)
{
    LOG_FUNCTION_NAME
    struct v4l2_capability cap;
    int ret = 0;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_QUERYCAP, &cap, "query cap");

    if (ret)
        return ret;

    memcpy(cardname, cap.card, sizeof(cap.card));

    return ret;
}

int __v4l2_overlay_set_output_fmt(int fd, uint32_t w, uint32_t h, uint32_t fmt)
{
    struct v4l2_format format;
    int ret = 0;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format, "get format");

    if (ret)
        return ret;

    format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    configure_pixfmt(&format.fmt.pix, fmt, w, h);

    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format, "set output format");

    return ret;
}

int __v4l2_overlay_set_overlay_fmt(int fd, uint32_t t, uint32_t l, uint32_t w, uint32_t h)
{
    struct v4l2_format format;
    struct v4l2_window *win;
    int ret = 0;

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_G_FMT, &format,
            "get v4l2_overlay format");

    if (ret)
        return ret;

    win = &format.fmt.win;
    win->w.left = l;
    win->w.top = t;
    win->w.width = w;
    win->w.height = h;

    format.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_S_FMT, &format,
            "set v4l2_overlay format");

    if (ret)
        return ret;

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

int __v4l2_overlay_req_buf(int fd, uint32_t* reqbufnum, v4l2_memory memtype)
{
    struct v4l2_requestbuffers reqbuf;
    int ret = 0;

    reqbuf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    reqbuf.memory = memtype;
    reqbuf.count = *reqbufnum;
    ret = v4l2_overlay_ioctl(fd, VIDIOC_REQBUFS, &reqbuf, "req bufs");

    if(ret)
        return ret;

    *reqbufnum = reqbuf.count;

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

int __v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len)
{
    struct v4l2_buffer buf;
    int ret = 0;

    ret = v4l2_overlay_query_buffer(fd, index, &buf);
    if (ret)
        return ret;

    if (is_mmaped(&buf))
        return -EINVAL;

    *len = buf.length;
    *start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
            fd, buf.m.offset);
    
    if (*start == MAP_FAILED)
        return -EINVAL;

    return 0;
}

int __v4l2_overlay_unmap_buf(void *start, size_t len)
{
    LOG_FUNCTION_NAME
    return munmap(start, len);
}

int __v4l2_overlay_stream_on(int fd)
{
    int ret = 0;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_STREAMON, &type, "stream on");

    if(ret) {
        ALOGE("%s: Stream on Failed!/%d", __func__, ret);
    }

    return ret;
}

int __v4l2_overlay_stream_off(int fd)
{
    int ret = 0;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_STREAMOFF, &type, "stream off");

    if(ret) {
        ALOGE("%s: Stream Off Failed!/%d", __func__, ret);
    }

    return ret;
}

int __v4l2_overlay_q_buf(int fd, unsigned length, int pPhyCAddr,
                             int buffer, v4l2_memory memtype)
{
    struct v4l2_buffer buf;
    int ret = 0;

    buf.index = buffer;
    buf.memory = memtype;
    buf.m.userptr = (unsigned long)pPhyCAddr;
    buf.length = length;

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    buf.flags = 0;

    return v4l2_overlay_ioctl(fd, VIDIOC_QBUF, &buf, "qbuf");
}

int __v4l2_overlay_dq_buf(int fd, int *index, v4l2_memory memtype)
{
    struct v4l2_buffer buf;
    int ret = 0;

    buf.type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    buf.memory = memtype;

    ret = v4l2_overlay_ioctl(fd, VIDIOC_DQBUF, &buf, "dqbuf");
    if (ret)
        return ret;

    *index = buf.index;
    return 0;
}

