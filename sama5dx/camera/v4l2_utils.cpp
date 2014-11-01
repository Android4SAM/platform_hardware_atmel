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

/* #define DEBUG 1 */
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

static void error(int fd, const char *msg)
{
    ALOGE("Error = %s from %s", strerror(errno), msg);
}

static int v4l2_ioctl(int fd, int req, void *arg, const char* msg)
{
    int ret = 0;
    ret = ioctl(fd, req, arg);
    if (ret < 0) {
        error(fd, msg);
        return -1;
    }
    return 0;
}

static int get_pixel_depth(unsigned int fmt)
{
    int depth = 0;

    switch (fmt) {
    case V4L2_PIX_FMT_NV12:
        depth = 12;
        break;
    case V4L2_PIX_FMT_NV21:
        depth = 12;
        break;
    case V4L2_PIX_FMT_YUV420:
        depth = 12;
        break;

    case V4L2_PIX_FMT_RGB565:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_YVYU:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUV422P:
        depth = 16;
        break;

    case V4L2_PIX_FMT_RGB32:
        depth = 32;
        break;
    }

    return depth;
}

#define v4l2_fourcc(a, b, c, d)  ((__u32)(a) | ((__u32)(b) << 8) | \
                                 ((__u32)(c) << 16) | ((__u32)(d) << 24))
/* 12  Y/CbCr 4:2:0 64x32 macroblocks */
#define V4L2_PIX_FMT_NV12T       v4l2_fourcc('T', 'V', '1', '2')

int __v4l2_querycap(int fd)
{
    LOG_FUNCTION_NAME
    struct v4l2_capability cap;
    int ret = 0;

    ret = v4l2_ioctl(fd, VIDIOC_QUERYCAP, &cap, "query cap");

    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QUERYCAP failed\n", __func__);
        return -1;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        ALOGE("ERR(%s):no capture devices\n", __func__);
        return -1;
    }

    return ret;
}

int __v4l2_enuminput(int fd, int index)
{
    struct v4l2_input input;
    int ret = 0;

    input.index = 0;
    ret = v4l2_ioctl(fd, VIDIOC_ENUMINPUT, &input, "enum input");
    if (ret != 0) {
        ALOGE("ERR(%s):No matching index found\n", __func__);
        return -1;
    }
    ALOGI("Name of input channel[%d] is %s\n", input.index, input.name);

    return ret;
}

int __v4l2_s_input(int fd, int index)
{
    struct v4l2_input input;
    int ret = 0;

    input.index = 0;
    ret = v4l2_ioctl(fd, VIDIOC_S_INPUT, &input, "set input");
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __func__);
        return ret;
    }

    return ret;
}

int __v4l2_set_fmt(int fd, uint32_t w, uint32_t h, uint32_t fmt)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    memset(&pixfmt, 0, sizeof(pixfmt));

    pixfmt.width = w;
    pixfmt.height = h;
    pixfmt.pixelformat = fmt;
    pixfmt.sizeimage = (w * h * get_pixel_depth(fmt)) / 8;
    pixfmt.field = V4L2_FIELD_NONE;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = v4l2_ioctl(fd, VIDIOC_S_FMT, &v4l2_fmt, "set format");
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return -1;
    }

    return ret;

}

int __v4l2_enum_fmt(int fd, uint32_t fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (v4l2_ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc, "enum format") == 0) {
        if (fmtdesc.pixelformat == fmt) {
            ALOGD("passed fmt = %#x found pixel format[%d]: %s\n", fmt, fmtdesc.index, fmtdesc.description);
            found = 1;
            break;
        }

        fmtdesc.index++;
    }

    if (!found) {
        ALOGE("unsupported pixel format\n");
        return -1;
    }

    return 0;
}

int __v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = v4l2_ioctl(fp, VIDIOC_S_PARM, streamparm, "set stream parameter");
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }

    return ret;
}

int __v4l2_req_buf(int fd, uint32_t* reqbufnum, v4l2_memory memtype)
{
    struct v4l2_requestbuffers reqbuf;
    int ret = 0;

    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = memtype;
    reqbuf.count = *reqbufnum;
    ret = v4l2_ioctl(fd, VIDIOC_REQBUFS, &reqbuf, "req bufs");

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

int __v4l2_query_buffer(int fd, int index, struct v4l2_buffer *buf)
{
    LOG_FUNCTION_NAME

    memset(buf, 0, sizeof(struct v4l2_buffer));

    buf->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf->memory = V4L2_MEMORY_MMAP;
    buf->index = index;
    ALOGV("query buffer, mem=%u type=%u index=%u\n",
         buf->memory, buf->type, buf->index);
    return v4l2_ioctl(fd, VIDIOC_QUERYBUF, buf, "querybuf ioctl");
}

int __v4l2_map_buf(int fd, int index, void **start, size_t *len)
{
    struct v4l2_buffer buf;
    int ret = 0;

    ret = __v4l2_query_buffer(fd, index, &buf);
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

int __v4l2_unmap_buf(void *start, size_t len)
{
    LOG_FUNCTION_NAME
    return munmap(start, len);
}

int __v4l2_stream_on(int fd)
{
    int ret = 0;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = v4l2_ioctl(fd, VIDIOC_STREAMON, &type, "stream on");

    if(ret) {
        ALOGE("%s: Stream on Failed!/%d", __func__, ret);
    }

    return ret;
}

int __v4l2_stream_off(int fd)
{
    int ret = 0;
    uint32_t type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = v4l2_ioctl(fd, VIDIOC_STREAMOFF, &type, "stream off");

    if(ret) {
        ALOGE("%s: Stream Off Failed!/%d", __func__, ret);
    }

    return ret;
}

int __v4l2_q_buf(int fd, unsigned length, int pPhyCAddr,
                             int buffer, v4l2_memory memtype)
{
    struct v4l2_buffer buf;
    int ret = 0;

    buf.index = buffer;
    buf.memory = memtype;
    buf.m.userptr = (unsigned long)pPhyCAddr;
    buf.length = length;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.field = V4L2_FIELD_NONE;
    buf.timestamp.tv_sec = 0;
    buf.timestamp.tv_usec = 0;
    buf.flags = 0;

    return v4l2_ioctl(fd, VIDIOC_QBUF, &buf, "qbuf");
}

int __v4l2_dq_buf(int fd, int *index, v4l2_memory memtype)
{
    struct v4l2_buffer buf;
    int ret = 0;

    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = memtype;

    ret = v4l2_ioctl(fd, VIDIOC_DQBUF, &buf, "dqbuf");
    if (ret)
        return ret;

    *index = buf.index;

    //ALOGD("%s: VIDIOC_DQBUF num is %d",__func__, index);
    return 0;
}

