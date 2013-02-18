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
** limitations under the License.
*/

#ifndef _V4L2CAMERA_H
#define _V4L2CAMERA_H

#define SKIP_PICTURE_FRAMES 10

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include <sys/mman.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/stat.h>

#include <linux/videodev2.h>

#include "ccrgb16toyuv420.h"

namespace android {

#if defined(LOG_NDEBUG) && LOG_NDEBUG == 0
#define LOG_CAMERA LOGD
#define LOG_CAMERA_PREVIEW LOGD

#define LOG_TIME_DEFINE(n) \
    struct timeval time_start_##n, time_stop_##n; unsigned long log_time_##n = 0;

#define LOG_TIME_START(n) \
    gettimeofday(&time_start_##n, NULL);

#define LOG_TIME_END(n) \
    gettimeofday(&time_stop_##n, NULL); log_time_##n = measure_time(&time_start_##n, &time_stop_##n);

#define LOG_TIME(n) \
    log_time_##n

#else
#define LOG_CAMERA(...)
#define LOG_CAMERA_PREVIEW(...)
#define LOG_TIME_DEFINE(n)
#define LOG_TIME_START(n)
#define LOG_TIME_END(n)
#define LOG_TIME(n)
#endif

#define CAMERA_DEV_NAME   "/dev/video1"

#define BPP             2
#define MIN(x, y)       (((x) < (y)) ? (x) : (y))
#define MAX_BUFFERS     3

#define V4L2_PIX_FMT_YVYU           v4l2_fourcc('Y', 'V', 'Y', 'U')

//From linux driver, ov2640.c.
#define MAX_BACK_CAMERA_PREVIEW_WIDTH 640
#define MAX_BACK_CAMERA_PREVIEW_HEIGHT 480

//The same as preview
#define MAX_BACK_CAMERA_SNAPSHOT_WIDTH 640
#define MAX_BACK_CAMERA_SNAPSHOT_HEIGHT 480

struct ISI_buffer {
    void    *start;
    size_t  length;
};

/* We use this struct as the v4l2_streamparm raw_data for
 * VIDIOC_G_PARM and VIDIOC_S_PARM
 */
struct sam_cam_parm {
	/* 1: use_preview; 0: use_capture */
	bool use_preview;
};

enum v4l2_focusmode {
        FOCUS_MODE_AUTO = 0,
        FOCUS_MODE_MACRO,
        FOCUS_MODE_FACEDETECT,
        FOCUS_MODE_AUTO_DEFAULT,
        FOCUS_MODE_MACRO_DEFAULT,
        FOCUS_MODE_FACEDETECT_DEFAULT,
        FOCUS_MODE_INFINITY,
        FOCUS_MODE_MAX,
};

class V4L2Camera {

public:

    enum CAMERA_ID {
        CAMERA_ID_BACK  = 0,
        CAMERA_ID_FRONT = 1,
    };

    V4L2Camera(); 
    ~V4L2Camera();

    static V4L2Camera* createInstance(void)
    {
        static V4L2Camera singleton;
        return &singleton;
    }
    
    int             initCamera(int index);
    void           resetCamera();
    void           DeinitCamera();

    int             setCameraId(int camera_id);
    int             getCameraId(void);
    int             getCameraFd(void);

    int             startPreview(void);
    int             stopPreview(void);
    int             getPreviewframe();
    int	       freePreviewframe(int index);
    int             setPreviewSize(int width, int height, int pixel_format);
    int             getPreviewSize(int *width, int *height, int *frame_size);
    int             getPreviewMaxSize(int *width, int *height);
    int             getPreviewPixelFormat(void);

    int             startRecord(void);
    int             stopRecord(void);
    int             getRecordFrame(void);
    int             releaseRecordFrame(int index);

    int             setSnapshotSize(int width, int height);
    int             getSnapshotSize(int *width, int *height, int *frame_size);
    int             getSnapshotMaxSize(int *width, int *height);
    int             setSnapshotPixelFormat(int pixel_format);
    int             getSnapshotPixelFormat(void);
    int             startSnapshot(void *rawbuf);
    int             stopSnapshot(void);
    
    int             SetRotate(int angle);
    int             getRotate(void);
    int             zoomIn(void);
    int             zoomOut(void);
    int             setZoom(int zoom_level);
    int             getZoom(void);
    int             previewPoll(bool preview);
    void           getPostViewConfig(int*, int*, int*);
    
    int             readjpeg (void *previewBuffer,int fileSize);
    int             SavePicture(void);
    int             savePicture(unsigned char *inputBuffer, const char * filename);
    void          convert(void *buf, void *rgb, int width, int height);
    void          rgb16TOyuv420(void *rgb16, void *yuv420);
	bool                	       mCaptureInProgress;
    
private:
    v4l2_streamparm m_streamparm;
	struct sam_cam_parm   *m_params;
    int             m_flag_init;
    int             m_camera_id;    
    int             m_cam_fd;
    int             m_angle;
    int             m_zoom_level;
    int             m_flag_camera_start;
    int             m_flag_record_start;

    int             m_preview_v4lformat;
    int             m_preview_width;
    int             m_preview_height;
    int             m_preview_max_width;
    int             m_preview_max_height;

    int             m_snapshot_v4lformat;
    int             m_snapshot_width;
    int             m_snapshot_height;
    int             m_snapshot_max_width;
    int             m_snapshot_max_height;    

    struct       pollfd   m_events_c;
    struct       ISI_buffer m_capture_buf;
    inline int      m_frameSize(int format, int width, int height);
    
    /* RGB->YUV conversion */
    CCRGB16toYUV420 *ccRGBtoYUV;
    int saveYUYVtoJPEG (unsigned char *inputBuffer, int width, int height, FILE *file, int quality);

	
};

}; // namespace android

#endif
