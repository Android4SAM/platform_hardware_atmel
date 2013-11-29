/*
** Copyright (C) 2009.11 Embest For AT91SAM9M10-Android2.0 camera By LiuXin- http://www.embedinfo.com/
** Copyright (C) 2009 0xlab.org - http://0xlab.org/
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License
*/
//#define LOG_NDEBUG 0
#define LOG_TAG "V4L2Camera"
#include <utils/Log.h>

#include "V4L2Camera.h"

extern "C" {
#include "jpeglib.h"
}

using namespace android;

#define CHECK(return_value)                                          \
    if (return_value < 0) {                                          \
        ALOGE("%s::%d fail. errno: %s, m_camera_id = %d\n",           \
             __func__, __LINE__, strerror(errno), m_camera_id);      \
        return -1;                                                   \
    }

#define CHECK_PTR(return_value)                                      \
    if (return_value < 0) {                                          \
        ALOGE("%s::%d fail, errno: %s, m_camera_id = %d\n",           \
             __func__,__LINE__, strerror(errno), m_camera_id);       \
        return NULL;                                                 \
    }

namespace android {

// ======================================================================
// Camera controls

static struct timeval time_start;
static struct timeval time_stop;

unsigned long measure_time(struct timeval *start, struct timeval *stop)
{
    unsigned long sec, usec, time;

    sec = stop->tv_sec - start->tv_sec;

    if (stop->tv_usec >= start->tv_usec) {
        usec = stop->tv_usec - start->tv_usec;
    } else {
        usec = stop->tv_usec + 1000000 - start->tv_usec;
        sec--;
    }

    time = (sec * 1000000) + usec;

    return time;
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

static int isi_poll(struct pollfd *events)
{
    int ret;

    /* 10 second delay is because sensor can take a long time
     * to do auto focus and capture in dark settings
     */
    ret = poll(events, 1, 10000);
    if (ret < 0) {
        ALOGE("ERR(%s):poll error\n", __func__);
        return ret;
    }

    if (ret == 0) {
        ALOGE("ERR(%s):No data in 10 secs..\n", __func__);
        return ret;
    }

    return ret;
}

static int isi_v4l2_querycap(int fp)
{
    struct v4l2_capability cap;
    int ret = 0;

    ret = ioctl(fp, VIDIOC_QUERYCAP, &cap);

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

static const __u8* isi_v4l2_enuminput(int fp, int index)
{
    static struct v4l2_input input;

//FIXME: Under linux driver, soc_camera.c, it only support input "0"
    input.index = 0;
    if (ioctl(fp, VIDIOC_ENUMINPUT, &input) != 0) {
        ALOGE("ERR(%s):No matching index found\n", __func__);
        return NULL;
    }
    ALOGI("Name of input channel[%d] is %s\n", input.index, input.name);

    return input.name;
}

static int isi_v4l2_s_input(int fp, int index)
{
    struct v4l2_input input;
    int ret;

//FIXME: Under linux driver, soc_camera.c, it only support input "0"
    input.index = 0;

    ret = ioctl(fp, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_INPUT failed\n", __func__);
        return ret;
    }

    return ret;
}

static int isi_v4l2_s_fmt(int fp, int width, int height, unsigned int fmt, int flag_capture)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    memset(&pixfmt, 0, sizeof(pixfmt));

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    pixfmt.field = V4L2_FIELD_NONE;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return -1;
    }

    return 0;
}

static int isi_v4l2_s_fmt_cap(int fp, int width, int height, unsigned int fmt)
{
    struct v4l2_format v4l2_fmt;
    struct v4l2_pix_format pixfmt;
    int ret;

    memset(&pixfmt, 0, sizeof(pixfmt));

    v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    pixfmt.width = width;
    pixfmt.height = height;
    pixfmt.pixelformat = fmt;
    if (fmt == V4L2_PIX_FMT_JPEG) {
        pixfmt.colorspace = V4L2_COLORSPACE_JPEG;
    }

    pixfmt.sizeimage = (width * height * get_pixel_depth(fmt)) / 8;

    v4l2_fmt.fmt.pix = pixfmt;

    /* Set up for capture */
    ret = ioctl(fp, VIDIOC_S_FMT, &v4l2_fmt);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_FMT failed\n", __func__);
        return ret;
    }

    return ret;
}

static int isi_v4l2_enum_fmt(int fp, unsigned int fmt)
{
    struct v4l2_fmtdesc fmtdesc;
    int found = 0;

    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmtdesc.index = 0;

    while (ioctl(fp, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
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

static int isi_v4l2_reqbufs(int fp, enum v4l2_buf_type type, int nr_bufs)
{
    struct v4l2_requestbuffers req;
    int ret;

    req.count = nr_bufs;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(fp, VIDIOC_REQBUFS, &req);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_REQBUFS failed\n", __func__);
        return -1;
    }

    return req.count;
}

static int isi_v4l2_querybuf(int fp, struct ISI_buffer *buffer, enum v4l2_buf_type type, int index)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    ALOGI("%s :", __func__);

    v4l2_buf.type = type;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp , VIDIOC_QUERYBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QUERYBUF failed\n", __func__);
        return -1;
    }

    buffer->length = v4l2_buf.length;
    if ((buffer->start = (char *)mmap(0, v4l2_buf.length,
                                      PROT_READ | PROT_WRITE, MAP_SHARED,
                                      fp, v4l2_buf.m.offset)) < 0) {
        ALOGE("%s %d] mmap() failed\n",__func__, __LINE__);
        return -1;
    }

    ALOGI("%s: buffer->start = %p v4l2_buf.length = %d",
         __func__, buffer->start, v4l2_buf.length);

    return 0;
}

static int isi_v4l2_streamon(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ret = ioctl(fp, VIDIOC_STREAMON, &type);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_STREAMON failed\n", __func__);
        return ret;
    }

    return ret;
}

static int isi_v4l2_streamoff(int fp)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int ret;

    ALOGV("%s :", __func__);
    ret = ioctl(fp, VIDIOC_STREAMOFF, &type);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_STREAMOFF failed\n", __func__);
        return ret;
    }

    return ret;
}

static int isi_v4l2_qbuf(int fp, int index)
{
    struct v4l2_buffer v4l2_buf;
    int ret;

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    ret = ioctl(fp, VIDIOC_QBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_QBUF failed\n", __func__);
        return ret;
    }

    return 0;
}

static int isi_v4l2_dqbuf(int fp)
{
    struct v4l2_buffer v4l2_buf;
    int ret;
    memset(&v4l2_buf,0,sizeof(v4l2_buf));

    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(fp, VIDIOC_DQBUF, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_DQBUF failed, dropped frame\n", __func__);
        return ret;
    }

    ALOGV("%s: VIDIOC_DQBUF num is %d",__func__,v4l2_buf.index);

    return v4l2_buf.index;
}

static int isi_v4l2_g_ctrl(int fp, unsigned int id)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;

    ret = ioctl(fp, VIDIOC_G_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s): VIDIOC_G_CTRL(id = 0x%x (%d)) failed, ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, ret);
        return ret;
    }

    return ctrl.value;
}

static int isi_v4l2_s_ctrl(int fp, unsigned int id, unsigned int value)
{
    struct v4l2_control ctrl;
    int ret;

    ctrl.id = id;
    ctrl.value = value;

    ret = ioctl(fp, VIDIOC_S_CTRL, &ctrl);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_CTRL(id = %#x (%d), value = %d) failed ret = %d\n",
             __func__, id, id-V4L2_CID_PRIVATE_BASE, value, ret);

        return ret;
    }

    return ctrl.value;
}

static int isi_v4l2_g_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_G_PARM, streamparm);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_G_PARM failed\n", __func__);
        return -1;
    }

    ALOGV("%s : you can print use preivew here\n", __func__);

    return 0;
}

static int isi_v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm)
{
    int ret;

    streamparm->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ret = ioctl(fp, VIDIOC_S_PARM, streamparm);
    if (ret < 0) {
        ALOGE("ERR(%s):VIDIOC_S_PARM failed\n", __func__);
        return ret;
    }

    return 0;
}

int V4L2Camera::previewPoll(bool preview)
{
    int ret;

    ALOGV("%s: enter",__func__);

    ret = poll(&m_events_c, 1, 1000);

    if (ret < 0) {
        ALOGE("ERR(%s):poll error\n", __func__);
        return ret;
    }

    if (ret == 0) {
        ALOGE("ERR(%s):No data in 1 secs.. Camera Device Reset \n", __func__);
        return ret;
    }

    ALOGV("%s: exit",__func__);

    return ret;
}

V4L2Camera::V4L2Camera ():
    m_flag_init(0),
    m_camera_id(CAMERA_ID_BACK),
    m_preview_v4lformat(V4L2_PIX_FMT_YUV422P),
    m_preview_width      (640),
    m_preview_height     (480),
    m_preview_max_width  (MAX_BACK_CAMERA_PREVIEW_WIDTH),
    m_preview_max_height (MAX_BACK_CAMERA_PREVIEW_HEIGHT),
    m_snapshot_v4lformat(V4L2_PIX_FMT_UYVY),
    m_snapshot_width      (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
    m_snapshot_height     (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
    m_snapshot_max_width  (MAX_BACK_CAMERA_SNAPSHOT_WIDTH),
    m_snapshot_max_height (MAX_BACK_CAMERA_SNAPSHOT_HEIGHT),
    m_angle(-1),
    m_flag_camera_start(0),
    m_zoom_level(-1)
{
    m_params = (struct sam_cam_parm*)&m_streamparm.parm.raw_data;
    memset(&m_capture_buf, 0, sizeof(m_capture_buf));
    ccRGBtoYUV = new CCRGB16toYUV420();
    ALOGV("%s :", __func__);
}

V4L2Camera::~V4L2Camera()
{
    ALOGV("%s :", __func__);
}

int V4L2Camera::initCamera(int index)
{
    ALOGV("%s :", __func__);
    int ret = 0;

    if (!m_flag_init) {
        m_cam_fd = open(CAMERA_DEV_NAME, O_RDWR);
        if (m_cam_fd < 0) {
            ALOGE("ERR(%s):Cannot open %s (error : %s)\n", __func__, CAMERA_DEV_NAME, strerror(errno));
            return -1;
        }
        ALOGD("initCamera: m_cam_fd(%d)", m_cam_fd);
        ret = isi_v4l2_querycap(m_cam_fd);
        CHECK(ret);
        if (!isi_v4l2_enuminput(m_cam_fd, index))
            return -1;
        ret = isi_v4l2_s_input(m_cam_fd, index);
        CHECK(ret);

        m_camera_id = index;
        switch (m_camera_id) {
        case CAMERA_ID_FRONT:
            ALOGE("We don't support a front camera");
            return -1;
            break;

        case CAMERA_ID_BACK:
            m_preview_max_width   = MAX_BACK_CAMERA_PREVIEW_WIDTH;
            m_preview_max_height  = MAX_BACK_CAMERA_PREVIEW_HEIGHT;
            break;
        }

        m_flag_init = 1;
    }
    return 0;
}

void V4L2Camera::resetCamera()
{
    ALOGV("%s :", __func__);
    DeinitCamera();
    initCamera(m_camera_id);
}

void V4L2Camera::DeinitCamera()
{
    ALOGV("%s :", __func__);

    if (m_flag_init) {

        stopRecord();

        /* close m_cam_fd after stopRecord() because stopRecord()
         * uses m_cam_fd to change frame rate
         */
        ALOGI("DeinitCamera: m_cam_fd(%d)", m_cam_fd);
        if (m_cam_fd > -1) {
            close(m_cam_fd);
            m_cam_fd = -1;
        }
        m_flag_init = 0;
    }
}

int V4L2Camera::getCameraFd(void)
{
    return m_cam_fd;
}

// ======================================================================
// Preview
int V4L2Camera::startPreview(void)
{
    v4l2_streamparm streamparm;
    ALOGV("%s :", __func__);

    if (m_flag_camera_start > 0) {
        ALOGE("ERR(%s):Preview was already started\n", __func__);
        return 0;
    }

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    /* enum_fmt, s_fmt sample */
    int ret = isi_v4l2_enum_fmt(m_cam_fd,m_preview_v4lformat);
    CHECK(ret);

    m_params->use_preview = 1;
    ret = isi_v4l2_s_parm(m_cam_fd, &m_streamparm);
    CHECK(ret);

    ret = isi_v4l2_s_fmt(m_cam_fd, m_preview_width,m_preview_height,m_preview_v4lformat, 0);
    CHECK(ret);

    ret = isi_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, MAX_BUFFERS);
    CHECK(ret);

    if(ccRGBtoYUV != NULL)
        ccRGBtoYUV->Init(m_preview_width, m_preview_height, m_preview_width, m_preview_width, m_preview_height, ((m_preview_width + 15) >> 4) << 4, 0);

    ALOGD("%s : m_preview_width: %d m_preview_height: %d m_angle: %d\n",
         __func__, m_preview_width, m_preview_height, m_angle);

    /* start with all buffers in queue */
    for (int i = 0; i < MAX_BUFFERS; i++) {
        ret = isi_v4l2_qbuf(m_cam_fd, i);
        CHECK(ret);
    }

    ret = isi_v4l2_streamon(m_cam_fd);
    CHECK(ret);

    m_flag_camera_start = 1;

    ret = isi_poll(&m_events_c);
    CHECK(ret);

    ALOGV("%s: got the first frame of the preview\n", __func__);

    return 0;
}

int V4L2Camera::stopPreview(void)
{
    int ret;

    ALOGV("%s :", __func__);

    if (m_flag_camera_start == 0) {
        ALOGW("%s: doing nothing because m_flag_camera_start is zero", __func__);
        return 0;
    }

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    ret = isi_v4l2_streamoff(m_cam_fd);
    CHECK(ret);

    m_flag_camera_start = 0;

    return ret;
}

int V4L2Camera::getPreviewframe()
{
    int index;
    int ret;

    if (m_flag_camera_start == 0 ) {
        ALOGE("ERR(%s):Start Camera Device Reset \n", __func__);

        stopPreview();
        ret = isi_v4l2_querycap(m_cam_fd);
        CHECK(ret);

        ret = startPreview();

        if (ret < 0) {
            ALOGE("ERR(%s): startPreview() return %d\n", __func__, ret);
            return 0;
        }
    }
    previewPoll(true);

    index = isi_v4l2_dqbuf(m_cam_fd);
    if (!(0 <= index && index < MAX_BUFFERS)) {
        ALOGE("ERR(%s):wrong index = %d\n", __func__, index);
        return -1;
    }

    return index;
}

int V4L2Camera::freePreviewframe(int index)
{
    ALOGV("%s(index(%d))",__func__,index);
    int ret;
    ret = isi_v4l2_qbuf(m_cam_fd, index);
    CHECK(ret);

    return ret;
}

int V4L2Camera::setPreviewSize(int width, int height, int pixel_format)
{
    ALOGV("%s(width(%d), height(%d), format(%d))", __func__, width, height, pixel_format);
    int v4lpixelformat = pixel_format;
#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
    if (v4lpixelformat == V4L2_PIX_FMT_YUV420)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUV420");
    else if (v4lpixelformat == V4L2_PIX_FMT_YUV422P)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUV422P");
    else if (v4lpixelformat == V4L2_PIX_FMT_YUYV)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_YUYV");
    else if (v4lpixelformat == V4L2_PIX_FMT_RGB565)
        ALOGV("PreviewFormat:V4L2_PIX_FMT_RGB565");
    else
        ALOGV("PreviewFormat:UnknownFormat");
#endif
    m_preview_width  = width;
    m_preview_height = height;
    m_preview_v4lformat = v4lpixelformat;

    return 0;
}

int V4L2Camera::getPreviewSize(int *width, int *height, int *frame_size)
{
    *width  = m_preview_width;
    *height = m_preview_height;
    *frame_size = m_frameSize(m_preview_v4lformat, m_preview_width, m_preview_height);

    return 0;
}

int V4L2Camera::getPreviewMaxSize(int *width, int *height)
{
    *width  = m_preview_max_width;
    *height = m_preview_max_height;

    return 0;
}

int V4L2Camera::getPreviewPixelFormat(void)
{
    return m_preview_v4lformat;
}

//Recording
int V4L2Camera::startRecord(void)
{
    int ret, i;

    ALOGV("%s :", __func__);

    if (m_flag_record_start > 0) {
        ALOGE("ERR(%s):Preview was already started\n", __func__);
        return 0;
    }

    m_flag_record_start = 1;

    return 0;
}

int V4L2Camera::stopRecord(void)
{
    int ret;
    ALOGV("%s :", __func__);

    if (m_flag_record_start == 0) {
        ALOGW("%s: doing nothing because m_flag_record_start is zero", __func__);
        return 0;
    }

    m_flag_record_start = 0;

    return 0;
}

int V4L2Camera::getRecordFrame()
{
    if (m_flag_record_start == 0) {
        ALOGE("%s: m_flag_record_start is 0", __func__);
        return -1;
    }

//	previewPoll(false);
//	return isi_v4l2_dqbuf(m_cam_fd2);
    return 0;
}

int V4L2Camera::releaseRecordFrame(int index)
{
    if (!m_flag_record_start) {
        ALOGI("%s: recording not in progress, ignoring", __func__);
        return 0;
    }

    //return isi_v4l2_qbuf(m_cam_fd2, index);
    return 0;
}

int V4L2Camera::setSnapshotSize(int width, int height)
{
    ALOGV("%s(width(%d), height(%d))", __func__, width, height);

    m_snapshot_width  = width;
    m_snapshot_height = height;

    return 0;
}

int V4L2Camera::getSnapshotSize(int *width, int *height, int *frame_size)
{
    *width  = m_snapshot_width;
    *height = m_snapshot_height;

    int frame = 0;

    frame = m_frameSize(m_snapshot_v4lformat, m_snapshot_width, m_snapshot_height);

    // set it big.
    if (frame == 0)
        frame = m_snapshot_width * m_snapshot_height * BPP;

    *frame_size = frame;

    return 0;
}

int V4L2Camera::getSnapshotMaxSize(int *width, int *height)
{
    switch (m_camera_id) {
    case CAMERA_ID_FRONT:
        ALOGE("We don't support a front camera");
        break;

    default:
    case CAMERA_ID_BACK:
        m_snapshot_max_width  = MAX_BACK_CAMERA_SNAPSHOT_WIDTH;
        m_snapshot_max_height = MAX_BACK_CAMERA_SNAPSHOT_HEIGHT;
        break;
    }

    *width  = m_snapshot_max_width;
    *height = m_snapshot_max_height;

    return 0;
}

int V4L2Camera::setSnapshotPixelFormat(int pixel_format)
{
    int v4lpixelformat= pixel_format;

    if (m_snapshot_v4lformat != v4lpixelformat) {
        m_snapshot_v4lformat = v4lpixelformat;
    }

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
    if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV420)
        ALOGE("%s : SnapshotFormat:V4L2_PIX_FMT_YUV420", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUV422P)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUV422P", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_YUYV)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_YUYV", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_UYVY)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_UYVY", __func__);
    else if (m_snapshot_v4lformat == V4L2_PIX_FMT_RGB565)
        ALOGD("%s : SnapshotFormat:V4L2_PIX_FMT_RGB565", __func__);
    else
        ALOGD("SnapshotFormat:UnknownFormat");
#endif
    return 0;
}

int V4L2Camera::getSnapshotPixelFormat(void)
{
    return m_snapshot_v4lformat;
}

int V4L2Camera::startSnapshot(void *rawbuf)
{
    v4l2_streamparm streamparm;
    ALOGV("%s : enter", __func__);

    if (m_flag_camera_start > 0) {
        ALOGE("ERR(%s):Preview is still running\n", __func__);
        return -1;
    }

    if (m_cam_fd <= 0) {
        ALOGE("ERR(%s):Camera was closed\n", __func__);
        return -1;
    }

    memset(&m_events_c, 0, sizeof(m_events_c));
    m_events_c.fd = m_cam_fd;
    m_events_c.events = POLLIN | POLLERR;

    /* enum_fmt, s_fmt sample */
    int ret = isi_v4l2_enum_fmt(m_cam_fd,m_snapshot_v4lformat);
    CHECK(ret);

    m_params->use_preview = 0;
    ret = isi_v4l2_s_parm(m_cam_fd, &m_streamparm);
    CHECK(ret);

    ret = isi_v4l2_s_fmt(m_cam_fd, m_snapshot_width,m_snapshot_height,m_snapshot_v4lformat, 0);
    CHECK(ret);

    ret = isi_v4l2_reqbufs(m_cam_fd, V4L2_BUF_TYPE_VIDEO_CAPTURE, 1);
    CHECK(ret);

    ALOGV("%s : m_snapshot_width: %d m_snapshot_height: %d m_angle: %d\n",
         __func__, m_snapshot_width, m_snapshot_height, m_angle);

    ret = isi_v4l2_querybuf(m_cam_fd, &m_capture_buf, V4L2_BUF_TYPE_VIDEO_CAPTURE, 0);
    CHECK(ret);

    ret = isi_v4l2_qbuf(m_cam_fd, 0);
    CHECK(ret);

    ret = isi_v4l2_streamon(m_cam_fd);
    CHECK(ret);

    ret = isi_poll(&m_events_c);
    CHECK(ret);

    isi_v4l2_dqbuf(m_cam_fd);

    for(int i=0; i < SKIP_PICTURE_FRAMES; i++) {
        ret = isi_v4l2_qbuf(m_cam_fd, 0);
        CHECK(ret);
        ret = isi_poll(&m_events_c);
        CHECK(ret);
        isi_v4l2_dqbuf(m_cam_fd);
    }

    memcpy(rawbuf, m_capture_buf.start, m_frameSize(m_snapshot_v4lformat, m_snapshot_width, m_snapshot_height));

    ALOGV("%s : exit", __func__);

    return 0;
}

int V4L2Camera::stopSnapshot(void)
{
    int ret;

    ALOGV("%s :", __func__);
    if (m_capture_buf.start) {
        munmap(m_capture_buf.start, m_capture_buf.length);
        ALOGI("munmap():virt. addr %p size = %d\n",
             m_capture_buf.start, m_capture_buf.length);
        m_capture_buf.start = NULL;
        m_capture_buf.length = 0;
    }

    ret = isi_v4l2_streamoff(m_cam_fd);
    CHECK(ret);

    return 0;
}

void V4L2Camera::getPostViewConfig(int *width, int *height, int *size)
{
    *width = 800;
    *height = 600;
    *size = 800 * 600 * 16 / 8;
    ALOGV("[5B] m_preview_width : %d, mPostViewWidth = %d mPostViewHeight = %d mPostViewSize = %d",
         m_preview_width, *width, *height, *size);
}

int V4L2Camera::SavePicture(void)
{
    return savePicture((unsigned char *)m_capture_buf.start, "/tmp/tmp.jpeg");
}

int V4L2Camera::savePicture(unsigned char *inputBuffer, const char * filename)
{
    FILE *output;
    int fileSize;
    int ret;
    output = fopen(filename, "wb");

    if (output == NULL) {
        ALOGE("GrabJpegFrame: Ouput file == NULL");
        return 0;
    }

    fileSize = saveYUYVtoJPEG(inputBuffer, m_snapshot_width, m_snapshot_height, output, 100);

    fclose(output);

    ALOGD("savePicture: saveYUYVtoJPEG OK\n");

    return fileSize;
}

int V4L2Camera::readjpeg(void *previewBuffer,int fileSize)
{
    FILE *input;
    input = fopen("/tmp/tmp.jpeg", "rb");
    if (input == NULL)
        ALOGE("readjpeg: Input file == NULL");
    else if(previewBuffer == NULL)
        ALOGE("readjpeg: previewBuffer == NULL");
    else {
        fread((uint8_t *)previewBuffer, 1, fileSize, input);
        fclose(input);
        ALOGD("read jpeg OK");
        return 0;
    }
    return fileSize;
}

int V4L2Camera::saveYUYVtoJPEG (unsigned char *inputBuffer, int width, int height, FILE *file, int quality)
{
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
    unsigned char *line_buffer;
    unsigned char Y1, Y2, U, V;
    register int r,g,b;
    unsigned int y=0;
    int line,col;

    int fileSize;

    line_buffer = (unsigned char *) calloc (width *3, 1);

    cinfo.err = jpeg_std_error (&jerr);
    jpeg_create_compress (&cinfo);
    jpeg_stdio_dest (&cinfo, file);

    cinfo.image_width = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults (&cinfo);
    jpeg_set_quality (&cinfo, quality, TRUE);

    jpeg_start_compress (&cinfo, TRUE);

    for (line = 0; line < height; line++) {
        unsigned char *ptr = line_buffer;
        for (col = 0; col < width; col+=2) {
            Y1 = inputBuffer[y + 0];
            V = inputBuffer[y + 1];
            Y2 = inputBuffer[y + 2];
            U = inputBuffer[y + 3];

            r = (1192 * (Y1 - 16) + 1634 * (V - 128) ) >> 10;
            g = (1192 * (Y1 - 16) - 833 * (V - 128) - 400 * (U -128) ) >> 10;
            b = (1192 * (Y1 - 16) + 2066 * (U - 128) ) >> 10;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            r = (1192 * (Y2 - 16) + 1634 * (V - 128) ) >> 10;
            g = (1192 * (Y2 - 16) - 833 * (V - 128) - 400 * (U -128) ) >> 10;
            b = (1192 * (Y2 - 16) + 2066 * (U - 128) ) >> 10;

            *(ptr++) = (r > 255) ? 255 : ((r < 0) ? 0 : r);
            *(ptr++) = (g > 255) ? 255 : ((g < 0) ? 0 : g);
            *(ptr++) = (b > 255) ? 255 : ((b < 0) ? 0 : b);

            y = y+4;
        }//line end

        row_pointer[0] = line_buffer;
        jpeg_write_scanlines (&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress (&cinfo);
    fileSize = ftell(file);
    jpeg_destroy_compress (&cinfo);

    free (line_buffer);

    return fileSize;

}

static inline void yuv_to_rgb16(unsigned char y,
                                unsigned char u,
                                unsigned char v,
                                unsigned char *rgb)
{
    register int r,g,b;
    int rgb16;

    r = (1192 * (y - 16) + 1634 * (v - 128) ) >> 10;
    g = (1192 * (y - 16) - 833 * (v - 128) - 400 * (u -128) ) >> 10;
    b = (1192 * (y - 16) + 2066 * (u - 128) ) >> 10;

    r = r > 255 ? 255 : r < 0 ? 0 : r;
    g = g > 255 ? 255 : g < 0 ? 0 : g;
    b = b > 255 ? 255 : b < 0 ? 0 : b;

    rgb16 = (int)(((r >> 3)<<11) | ((g >> 2) << 5)| ((b >> 3) << 0));

    *rgb = (unsigned char)(rgb16 & 0xFF);
    rgb++;
    *rgb = (unsigned char)((rgb16 & 0xFF00) >> 8);

}

void V4L2Camera::convert(void *buf_in, void *rgb_in, int width, int height)
{
    int x,y,z=0;
    int blocks;

    unsigned char *buf = (unsigned char *) buf_in;
    unsigned char *rgb = (unsigned char *) rgb_in;

    blocks = (width * height) * 2;

    for (y = 0; y < blocks; y+=4) {
        unsigned char Y1, Y2, U, V;

        U = buf[y + 0];
        Y1 = buf[y + 1];
        V = buf[y + 2];
        Y2 = buf[y + 3];

        yuv_to_rgb16(Y1, U, V, &rgb[y]);
        yuv_to_rgb16(Y2, U, V, &rgb[y + 2]);
    }
}

int V4L2Camera::setCameraId(int camera_id)
{

    m_camera_id = camera_id;
    return 0;
}

int V4L2Camera::getCameraId(void)
{
    return m_camera_id;
}

int V4L2Camera::SetRotate(int angle)
{
    if (m_angle != angle) {
        switch (angle) {
        case -360:
        case    0:
        case  360:
            m_angle = 0;
            break;

        case -270:
        case   90:
            m_angle = 90;
            break;

        case -180:
        case  180:
            m_angle = 180;
            break;

        case  -90:
        case  270:
            m_angle = 270;
            break;

        default:

            return -1;
        }

    }

    return 0;
}

int V4L2Camera::zoomIn(void)
{
    return 0;
}

int V4L2Camera::zoomOut(void)
{
    return 0;
}

int V4L2Camera::setZoom(int zoom_level)
{
    return 0;
}

int V4L2Camera::getZoom(void)
{
    return m_zoom_level;
}

int V4L2Camera::getRotate(void)
{
    return m_angle;
}

void V4L2Camera::rgb16TOyuv420(void *rgb16, void *yuv420)
{
    if(ccRGBtoYUV != NULL)
        ccRGBtoYUV->Convert((unsigned char *)rgb16, (unsigned char *)yuv420);
    else
        ALOGE("ccRGBtoYUV is null, we could not convert data from rgb565 to yuv420");
}

// ======================================================================
// Conversions

inline int V4L2Camera::m_frameSize(int format, int width, int height)
{
    int size = 0;

    switch (format) {
    case V4L2_PIX_FMT_YUV420:
        size = (width * height * 3 / 2);
        break;

    case V4L2_PIX_FMT_YUV422P:
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
        size = (width * height * 2);
        break;

    case V4L2_PIX_FMT_RGB565:
        size = (width * height * BPP);
        break;

    default :
        ALOGE("ERR(%s):Invalid V4L2 pixel format(%d)\n", __func__, format);

    }

    return size;
}

}; // namespace android
/*
 * 2010.04.17 Final V1 ,Embest Liu Xin
*/
