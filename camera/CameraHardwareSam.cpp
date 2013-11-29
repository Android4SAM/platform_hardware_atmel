/*
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
#define LOG_TAG "CameraHardwareSam"
#include <utils/Log.h>
#include "V4L2Camera.h"
#include "CameraHardwareSam.h"
#include "converter.h"
#include <camera/Camera.h>
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <hardware/hardware.h>
#include <signal.h>

extern "C" {
#include<unistd.h>
#include <pthread.h>
#include <signal.h>
}
#include "cutils/properties.h"

namespace android {
static const int INITIAL_SKIP_FRAME = 3;
static const int EFFECT_SKIP_FRAME = 1;
bool CameraHardwareSam::mInitialed = false;
gralloc_module_t const* CameraHardwareSam::mGrallocHal;

CameraHardwareSam::CameraHardwareSam(int cameraId, camera_device_t *dev)
    : mCaptureInProgress(false),
      mParameters(),
      mPreviewHeap(0),
      mRawHeap(0),
      mV4L2Camera(NULL),
#if defined(BOARD_USES_OVERLAY)
      mUseOverlay(false),
      mOverlayBufferIdx(0),
#endif
      mNotifyCb(0),
      mDataCb(0),
      mDataCbTimestamp(0),
      mCallbackCookie(0),
      mMsgEnabled(0),
      mRecordRunning(false),
      m_numOfAvailableRecordBuf(0)
{
    ALOGV("%s :", __func__);
    int ret = 0;
    char value[PROPERTY_VALUE_MAX];
    mPreviewWindow = NULL;
    mV4L2Camera = V4L2Camera::createInstance();
    mRawHeap = NULL;
    mPreviewHeap = NULL;
    for (int i = 0; i < NUM_OF_RECORD_BUF; i++)
        mRecordHeap[i] = NULL;

    m_cntRecordBuf = 0;

    if (!mGrallocHal) {
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
        if (ret)
            ALOGE("ERR(%s):Fail on loading gralloc HAL", __func__);
    }
    if(mV4L2Camera == NULL)
    {
        ALOGE("ERR(%s):Fail on mV4L2Camera object creation", __func__);
        return;
    }

    ret = mV4L2Camera->initCamera(cameraId);

    if (ret < 0) {
        ALOGE("ERR(%s):Fail on mV4L2Camera init", __func__);
        return;
    }

    initDefaultParameters(cameraId);

    mExitAutoFocusThread = false;
    mExitPreviewThread = false;
    /* whether the PreviewThread is active in preview or stopped.  we
     * create the thread but it is initially in stopped state.
     */
    mPreviewRunning = false;
    mPreviewStartDeferred = false;
    mPreviewThread = new PreviewThread(this);
    mPictureThread = new PictureThread(this);
    mAutoFocusThread = new AutoFocusThread(this);
    mInitialed = true;
}

int CameraHardwareSam::getCameraId() const
{
    return mV4L2Camera->getCameraId();
}

void CameraHardwareSam::initDefaultParameters(int cameraId)
{
    if (mV4L2Camera == NULL) {
        ALOGE("ERR(%s):mV4L2Camera object is NULL", __func__);
        return;
    }

    CameraParameters p;

    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
          "1600x1200,1280x1024,1024x768,800x600,640x480,352x288,320x240,176x144");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
          "640x480");

    int preview_max_width   = 0;
    int preview_max_height  = 0;
    int snapshot_max_width  = 0;
    int snapshot_max_height = 0;

    if (mV4L2Camera->getPreviewMaxSize(&preview_max_width,
                                       &preview_max_height) < 0)
        ALOGE("getPreviewMaxSize fail (%d / %d) \n",
             preview_max_width, preview_max_height);

    if (mV4L2Camera->getSnapshotMaxSize(&snapshot_max_width,
                                        &snapshot_max_height) < 0)
        ALOGE("getSnapshotMaxSize fail (%d / %d) \n",
             snapshot_max_width, snapshot_max_height);

    p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.setPreviewSize(preview_max_width, preview_max_height);

    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.setPictureSize(snapshot_max_width, snapshot_max_height);
    p.set(CameraParameters::KEY_JPEG_QUALITY, "100"); // maximum quality

    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS,
          CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS,
          CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,
          "yuv420p");

    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, "100");

    p.set(CameraParameters::KEY_ROTATION, 0);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_DAYLIGHT);

    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE, "(15000,30000)");
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "15000,30000");
    p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "51.2");
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "39.4");

    p.setPreviewFrameRate(20);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, "5,10,15,20,30");

    String8 parameterString;
    parameterString = CameraParameters::FOCUS_MODE_FIXED;
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES,
          parameterString.string());
    p.set(CameraParameters::KEY_FOCUS_MODE,
          CameraParameters::FOCUS_MODE_FIXED);
    p.set(CameraParameters::KEY_FOCUS_DISTANCES,
          FRONT_CAMERA_FOCUS_DISTANCES_STR);

    mParameters = p;
    if (setParameters(p) != NO_ERROR) {
        ALOGE("Failed to set default parameters?!");
    }
}

CameraHardwareSam::~CameraHardwareSam()
{
    ALOGV("%s :", __func__);
}

void CameraHardwareSam::setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void *user)
{
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemoryCb = get_memory;
    mCallbackCookie = user;
}

void CameraHardwareSam::enableMsgType(int32_t msgType)
{
    ALOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled |= msgType;
    ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

void CameraHardwareSam::disableMsgType(int32_t msgType)
{
    ALOGV("%s : msgType = 0x%x, mMsgEnabled before = 0x%x",
         __func__, msgType, mMsgEnabled);
    mMsgEnabled &= ~msgType;
    ALOGV("%s : mMsgEnabled = 0x%x", __func__, mMsgEnabled);
}

bool CameraHardwareSam::msgTypeEnabled(int32_t msgType)
{
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------

static void showFPS(const char *tag)
{
    static int mFrameCount = 0;
    static int mLastFrameCount = 0;
    static nsecs_t mLastFpsTime = 0;
    static float mFps = 0;
    mFrameCount++;
    if (!(mFrameCount & 0x1F)) {
        nsecs_t now = systemTime();
        nsecs_t diff = now - mLastFpsTime;
        mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
        mLastFpsTime = now;
        mLastFrameCount = mFrameCount;
        ALOGD("[%s] %d Frames, %f FPS", tag, mFrameCount, mFps);
    }
}

int CameraHardwareSam::previewThreadWrapper()
{
    ALOGD("%s: starting", __func__);
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            ALOGD("%s: calling mV4L2Camera->stopPreview() and waiting", __func__);
            mV4L2Camera->stopPreview();
            /* signal that we're stopping */
            mPreviewStoppedCondition.signal();
            mPreviewCondition.wait(mPreviewLock);
            ALOGD("%s: return from wait", __func__);
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
            ALOGD("%s: exiting", __func__);
            mV4L2Camera->stopPreview();
            return 0;
        }
        previewThread();
    }
}

int CameraHardwareSam::previewThread()
{
    int index = 0;
    int width, height, frame_size, offset, page_size;
    nsecs_t timestamp;

    ALOGV("%s:",__func__);

    index = mV4L2Camera->getPreviewframe();
    if (index < 0) {
        ALOGE("ERR(%s):Fail on mV4L2Camera->getPreview()", __func__);
        return UNKNOWN_ERROR;
    }

    if (index == kBufferCount) {
        mV4L2Camera->freePreviewframe(index);
        return NO_ERROR;
    }

    mV4L2Camera->getPreviewSize(&width, &height, &frame_size);
    page_size = getpagesize();
    offset = ((frame_size + (page_size - 1)) & (~(page_size - 1))) * index;

    ALOGV("mPreviewHeap(fd(%d), size(%d), width(%d), height(%d))",
         mV4L2Camera->getCameraFd(), frame_size, width, height);

    if (mPreviewWindow && mGrallocHal) {
        buffer_handle_t *buf_handle;
        int stride;
        if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride)) {
            ALOGE("Could not dequeue gralloc buffer!\n");
            goto callbacks;
        }

        void *vaddr;
        if (!mGrallocHal->lock(mGrallocHal,
                               *buf_handle,
                               GRALLOC_USAGE_SW_WRITE_OFTEN,
                               0, 0, width, height, &vaddr)) {
            char *frame = ((char *)mPreviewHeap->data) + offset;

            // the code below assumes YUV, not RGB
            {
                int h;
                char *src = frame;
                char *ptr = (char *)vaddr;
                memcpy(ptr, src, frame_size);
                //YUY2toYV12(frame, vaddr, width, height);
            }
            mGrallocHal->unlock(mGrallocHal, *buf_handle);
        }
        else
            ALOGE("%s: could not obtain gralloc buffer", __func__);

        if (0 != mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle)) {
            ALOGE("Could not enqueue gralloc buffer!\n");
            goto callbacks;
        }
    }
callbacks:
    // Notify the client of a new frame.
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewHeap, index, NULL, mCallbackCookie);
    }

    char *preview_frame = ((char *)mPreviewHeap->data) + offset;

    if (mRecordRunning && (m_numOfAvailableRecordBuf > 0) && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
        Mutex::Autolock lock(mRecordLock);

        yuyv422_to_yuv420((unsigned char*) preview_frame, (unsigned char*) mRecordHeap[m_cntRecordBuf]->data, width, height);

        timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
        mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME, mRecordHeap[m_cntRecordBuf], 0, mCallbackCookie);

        m_numOfAvailableRecordBuf--;
        if (m_numOfAvailableRecordBuf < 0)
            m_numOfAvailableRecordBuf = 0;

        m_cntRecordBuf++;
        if (m_cntRecordBuf == NUM_OF_RECORD_BUF)
            m_cntRecordBuf = 0;
    }

    mV4L2Camera->freePreviewframe(index);
    return NO_ERROR;
}

void CameraHardwareSam::setSkipFrame(int frame)
{
    Mutex::Autolock lock(mSkipFrameLock);
    if (frame < mSkipFrame)
        return;

    mSkipFrame = frame;
}


status_t CameraHardwareSam::startPreview()
{
    int ret = 0;        //s1 [Apply factory standard]

    ALOGV("%s :", __func__);

    if (waitCaptureCompletion() != NO_ERROR) {
        return TIMED_OUT;
    }

    mPreviewLock.lock();
    if (mPreviewRunning) {
        // already running
        ALOGE("%s : preview thread already running", __func__);
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }

    mPreviewRunning = true;
    mPreviewStartDeferred = false;

    if (!mPreviewWindow) {
        ALOGI("%s : deferring", __func__);
        mPreviewStartDeferred = true;
        mPreviewLock.unlock();
        return NO_ERROR;
    }

    ret = startPreviewInternal();
    if (ret == OK)
        mPreviewCondition.signal();

    mPreviewLock.unlock();
    return ret;
}

status_t CameraHardwareSam::startPreviewInternal()
{
    ALOGV("%s", __func__);

    int ret  = mV4L2Camera->startPreview();

    if (ret < 0) {
        ALOGE("ERR(%s):Fail on mSamCamera->startPreview()", __func__);
        return UNKNOWN_ERROR;
    }

    setSkipFrame(INITIAL_SKIP_FRAME);

    int width, height, frame_size, page_size, aligned_buffer_size;

    mV4L2Camera->getPreviewSize(&width, &height, &frame_size);
    page_size = getpagesize();
    aligned_buffer_size = (frame_size + (page_size - 1)) & (~(page_size - 1));

    if (mPreviewHeap) {
        mPreviewHeap->release(mPreviewHeap);
        mPreviewHeap = 0;
    }

    mPreviewHeap = mGetMemoryCb((int)mV4L2Camera->getCameraFd(),
                                aligned_buffer_size,
                                kBufferCount,
                                0); // no cookie

    mV4L2Camera->getPostViewConfig(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);

    return NO_ERROR;
}

void CameraHardwareSam::stopPreviewInternal()
{
    ALOGV("%s :", __func__);

    /* request that the preview thread stop. */
    if (mPreviewRunning) {
        mPreviewRunning = false;
        if (!mPreviewStartDeferred) {
            mPreviewCondition.signal();
            /* wait until preview thread is stopped */
            mPreviewStoppedCondition.wait(mPreviewLock);
        }
        else
            ALOGV("%s : preview running but deferred, doing nothing", __func__);
    } else
        ALOGI("%s : preview not running, doing nothing", __func__);
}

void CameraHardwareSam::stopPreview() {
    ALOGV("%s :", __func__);

    /* request that the preview thread stop. */
    mPreviewLock.lock();
    stopPreviewInternal();
    mPreviewLock.unlock();

}

bool CameraHardwareSam::previewEnabled()	{
    Mutex::Autolock lock(mPreviewLock);
    ALOGD("%s : %d", __func__, mPreviewRunning);
    return mPreviewRunning;
}

// ---------------------------------------------------------------------------

int CameraHardwareSam::autoFocusThread()
{
    ALOGV("%s : starting", __func__);

    /* block until we're told to start.  we don't want to use
     * a restartable thread and requestExitAndWait() in cancelAutoFocus()
     * because it would cause deadlock between our callbacks and the
     * caller of cancelAutoFocus() which both want to grab the same lock
     * in CameraServices layer.
     */
    mFocusLock.lock();
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request0", __func__);
        return NO_ERROR;
    }
    mFocusCondition.wait(mFocusLock);
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        ALOGV("%s : exiting on request1", __func__);
        return NO_ERROR;
    }
    mFocusLock.unlock();

    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);

    ALOGV("%s : exiting with no error", __func__);
    return NO_ERROR;
}

status_t CameraHardwareSam::autoFocus()
{
    ALOGV("%s :", __func__);
    /* signal autoFocusThread to run once */
    mFocusCondition.signal();
    return NO_ERROR;
}

status_t CameraHardwareSam::cancelAutoFocus()
{
    ALOGV("%s :", __func__);

    // cancelAutoFocus should be allowed after preview is started. But if
    // the preview is deferred, cancelAutoFocus will fail. Ignore it if that is
    // the case.
    if (mPreviewRunning && mPreviewStartDeferred) return NO_ERROR;

    return NO_ERROR;
}

status_t CameraHardwareSam::dump(int fd) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    const Vector<String16> args;
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t CameraHardwareSam::storeMetaDataInBuffers(bool enable)
{
    // FIXME:
    // metadata buffer mode can be turned on or off.
    // Samsung needs to fix this.
    if (enable) {
        ALOGE("Metadata buffer mode is not supported!");
        return INVALID_OPERATION;
    }
    return OK;
}


status_t CameraHardwareSam::setPreviewWindow(preview_stream_ops *w)
{
    int min_bufs;

    mPreviewWindow = w;
    ALOGD("%s: mPreviewWindow %p", __func__, mPreviewWindow);

    if (!w) {
        ALOGV("preview window is NULL!");
        return OK;
    }

    mPreviewLock.lock();

    if (mPreviewRunning && !mPreviewStartDeferred) {
        ALOGI("stop preview (window change)");
        stopPreviewInternal();
    }

    if (w->get_min_undequeued_buffer_count(w, &min_bufs)) {
        ALOGE("%s: could not retrieve min undequeued buffer count", __func__);
        return INVALID_OPERATION;
    }

    if (min_bufs >= kBufferCount) {
        ALOGE("%s: min undequeued buffer count %d is too high (expecting at most %d)", __func__,
             min_bufs, kBufferCount - 1);
    }

    ALOGV("%s: setting buffer count to %d", __func__, kBufferCount);
    if (w->set_buffer_count(w, kBufferCount)) {
        ALOGE("%s: could not set buffer count", __func__);
        return INVALID_OPERATION;
    }

    int preview_width;
    int preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    int hal_pixel_format = HAL_PIXEL_FORMAT_YCbCr_422_I;

    const char *str_preview_format = mParameters.getPreviewFormat();

    if (w->set_usage(w, GRALLOC_USAGE_SW_WRITE_OFTEN)) {
        ALOGE("%s: could not set usage on gralloc buffer", __func__);
        return INVALID_OPERATION;
    }

    if (w->set_buffers_geometry(w,
                                preview_width, preview_height,
                                hal_pixel_format)) {
        ALOGE("%s: could not set buffers geometry to %s",
             __func__, str_preview_format);
        return INVALID_OPERATION;
    }
    if (mPreviewRunning && mPreviewStartDeferred) {
        ALOGD("start/resume preview");
        status_t ret = startPreviewInternal(); //startPreview();
        if (ret == OK) {
            mPreviewStartDeferred = false;
            mPreviewCondition.signal();
        }
    }
    mPreviewLock.unlock();

    return OK;
}


status_t CameraHardwareSam::sendCommand(int32_t command, int32_t arg1,
                                        int32_t arg2) {
    return NO_ERROR;
}

// ---------------------------------------------------------------------------

status_t CameraHardwareSam::startRecording()
{
    ALOGD("%s :", __func__);

    int width, height, frame_size;

    Mutex::Autolock lock(mRecordLock);

    mV4L2Camera->getPreviewSize(&width, &height, &frame_size);

    for (int i = 0; i < NUM_OF_RECORD_BUF; i++) {
        if (mRecordHeap[i] != NULL) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = NULL;
        }

        mRecordHeap[i] = mGetMemoryCb(-1,  (width * height * 3) >> 1, 1, NULL);
    }

    if (mRecordRunning == false) {
        if (mV4L2Camera->startRecord() < 0) {
            ALOGE("ERR(%s):Fail on mV4L2Camera->startRecord()", __func__);
            return UNKNOWN_ERROR;
        }

        m_numOfAvailableRecordBuf = NUM_OF_RECORD_BUF;
        mRecordRunning = true;
    }

    return NO_ERROR;
}

void CameraHardwareSam::stopRecording()
{
    ALOGD("%s :", __func__);

    Mutex::Autolock lock(mRecordLock);

    for (int i = 0; i < NUM_OF_RECORD_BUF; i++) {
        if (mRecordHeap[i] != NULL) {
            mRecordHeap[i]->release(mRecordHeap[i]);
            mRecordHeap[i] = NULL;
        }
    }

    if (mRecordRunning == true) {
        if (mV4L2Camera->stopRecord() < 0) {
            ALOGE("ERR(%s):Fail on mV4L2Camera->stopRecord()", __func__);
            return;
        }
        mRecordRunning = false;
    }

}

bool CameraHardwareSam::recordingEnabled()
{
    ALOGD("%s :", __func__);

    return mRecordRunning;
}

void CameraHardwareSam::releaseRecordingFrame(const void *opaque)
{
    struct addrs *addrs = (struct addrs *)opaque;
    int i, index;
    bool find = false;

    ALOGV("%s :(addr=%p)", __func__, opaque);

    for (i = 0; i < NUM_OF_RECORD_BUF; i++) {
        if ((char *)mRecordHeap[i]->data == (char *)opaque) {
            find = true;
            break;
        }
    }

    if (find == true) {
        index = i;
        m_numOfAvailableRecordBuf++;
        if (NUM_OF_RECORD_BUF <= m_numOfAvailableRecordBuf)
            m_numOfAvailableRecordBuf = NUM_OF_RECORD_BUF;
    } else {
        ALOGI("DEBUG(%s):no matched index(%p)", __func__, (char *)opaque);
    }
}

int CameraHardwareSam::pictureThread()
{
    int ret;
    int filesize;
    int width, height;
    int cap_width, cap_height, cap_frame_size;

    stopPreview();
    mV4L2Camera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
    int mJpegHeapSize = cap_frame_size;

    if (mRawHeap) {
        mRawHeap->release(mRawHeap);
        mRawHeap = 0;
    }
    mRawHeap = mGetMemoryCb(-1, mJpegHeapSize, 1, 0);

    camera_memory_t *JpegHeap = mGetMemoryCb(-1, mJpegHeapSize, 1, 0);

    ret = mV4L2Camera->startSnapshot(mRawHeap->data);
    if(ret != 0) {
        ALOGE("%s:could not start capture",__func__);
        goto out;
    }

    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        mDataCb(CAMERA_MSG_RAW_IMAGE, mRawHeap, 0, NULL, mCallbackCookie);
    }

    mV4L2Camera->SavePicture();

    ret = mV4L2Camera->readjpeg(JpegHeap->data, mJpegHeapSize);

    if(ret != 0) {
        ALOGE("%s:read jpeg error",__func__);
        goto out;
    }

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegHeap, 0, NULL, mCallbackCookie);
    }

    ret = NO_ERROR;

out:
    JpegHeap->release(JpegHeap);
    mV4L2Camera->stopSnapshot();
    mCaptureLock.lock();
    mCaptureInProgress = false;
    mCaptureCondition.broadcast();
    mCaptureLock.unlock();

    return ret;
}

status_t CameraHardwareSam::waitCaptureCompletion() {
    // 5 seconds timeout
    nsecs_t endTime = 5000000000LL + systemTime(SYSTEM_TIME_MONOTONIC);
    Mutex::Autolock lock(mCaptureLock);
    while (mCaptureInProgress) {
        nsecs_t remainingTime = endTime - systemTime(SYSTEM_TIME_MONOTONIC);
        if (remainingTime <= 0) {
            ALOGE("Timed out waiting picture thread.");
            return TIMED_OUT;
        }
        ALOGD("Waiting for picture thread to complete.");
        mCaptureCondition.waitRelative(mCaptureLock, remainingTime);
    }
    return NO_ERROR;
}

status_t CameraHardwareSam::takePicture()
{
    ALOGV("%s :", __func__);

    if (mCaptureInProgress) {
        ALOGE("%s : capture already in progress", __func__);
        return INVALID_OPERATION;
    }

    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) != NO_ERROR) {
        ALOGE("%s : couldn't run picture thread", __func__);
        return INVALID_OPERATION;
    }
    mCaptureLock.lock();
    mCaptureInProgress = true;
    mCaptureLock.unlock();

    return NO_ERROR;
}

status_t CameraHardwareSam::cancelPicture()
{
    mPictureThread->requestExitAndWait();

    return NO_ERROR;
}

status_t CameraHardwareSam::setParameters(const CameraParameters& params) {
    ALOGD("%s :", __func__);

    status_t ret = NO_ERROR;

    // preview size
    int new_preview_width  = 0;
    int new_preview_height = 0;
    params.getPreviewSize(&new_preview_width, &new_preview_height);
    const char *new_str_preview_format = params.getPreviewFormat();

    if (0 < new_preview_width && 0 < new_preview_height &&
            new_str_preview_format != NULL ) {
        int new_preview_format = 0;
        if (!strcmp(new_str_preview_format,
                    CameraParameters::PIXEL_FORMAT_YUV420SP))
            new_preview_format = V4L2_PIX_FMT_YUYV;
        else
            ALOGE("ERR: not a supported preview format");

        if (mV4L2Camera->setPreviewSize(new_preview_width, new_preview_height, new_preview_format) < 0) {
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPreviewSize(new_preview_width, new_preview_height);
            mParameters.setPreviewFormat(new_str_preview_format);
        }
    } else {
        ALOGE("%s: Invalid preview size(%dx%d)",
             __func__, new_preview_width, new_preview_height);

        ret = INVALID_OPERATION;
    }

    int new_picture_width  = 0;
    int new_picture_height = 0;

    params.getPictureSize(&new_picture_width, &new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height) {
        if (mV4L2Camera->setSnapshotSize(new_picture_width, new_picture_height) < 0) {
            ALOGE("ERR(%s):Fail on mV4L2Camera->setSnapshotSize(width(%d), height(%d))",
                 __func__, new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPictureSize(new_picture_width, new_picture_height);
        }
    }

    // picture format
    const char *new_str_picture_format = params.getPictureFormat();
    ALOGD("%s : new_str_picture_format %s", __func__, new_str_picture_format);
    if (new_str_picture_format != NULL) {
        int new_picture_format = 0;
        if (!strcmp(new_str_picture_format, CameraParameters::PIXEL_FORMAT_RGB565))
            new_picture_format = V4L2_PIX_FMT_RGB565;
        else if (!strcmp(new_str_picture_format, CameraParameters::PIXEL_FORMAT_JPEG))
            new_picture_format = V4L2_PIX_FMT_YUYV;
        else
            ALOGE("ERR: not a supported picture format");

        if (mV4L2Camera->setSnapshotPixelFormat(new_picture_format) < 0) {
            ALOGE("ERR(%s):Fail on mV4L2Camera->setSnapshotPixelFormat(format(%d))", __func__, new_picture_format);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.setPictureFormat(new_str_picture_format);
        }
    }

    // frame rate
    int new_frame_rate = params.getPreviewFrameRate();
    if (new_frame_rate != mParameters.getPreviewFrameRate()) {
        mParameters.setPreviewFrameRate(new_frame_rate);
    }

    int new_rotation = params.getInt(CameraParameters::KEY_ROTATION);
    if (0 <= new_rotation) {
        ALOGD("%s : set orientation:%d\n", __func__, new_rotation);
        if (mV4L2Camera->SetRotate(new_rotation) < 0) {
            ALOGE("ERR(%s):Fail on mV4L2Camera->SetRotate(%d)", __func__, new_rotation);
            ret = UNKNOWN_ERROR;
        } else {
            mParameters.set(CameraParameters::KEY_ROTATION, new_rotation);
        }
    }

    const char *new_focus_mode_str = params.get(CameraParameters::KEY_FOCUS_MODE);

    // focus mode
    if (new_focus_mode_str != NULL) {
        int  new_focus_mode = -1;
        if (!strcmp(new_focus_mode_str,
                    CameraParameters::FOCUS_MODE_AUTO)) {
        }
        else if (!strcmp(new_focus_mode_str,
                         CameraParameters::FOCUS_MODE_FIXED)) {
        }
        else {
            ALOGE("%s::unmatched focus_mode(%s)", __func__, new_focus_mode_str);
            //ret = UNKNOWN_ERROR;
        }
        if (0 <= new_focus_mode) {
            if (mV4L2Camera/*->setFocusMode(new_focus_mode) < 0*/) {
                ALOGE("%s::mV4L2Camera->setFocusMode(%d) fail", __func__, new_focus_mode);
                ret = UNKNOWN_ERROR;
            } else {
                mParameters.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_FIXED);
            }
        }
    }

    return ret;
}

CameraParameters CameraHardwareSam::getParameters() const
{
    ALOGD("%s :", __func__);
    return mParameters;
}

void CameraHardwareSam::release()
{
    ALOGD("%s :", __func__);
    /* shut down any threads we have that might be running.  do it here
       * instead of the destructor.  we're guaranteed to be on another thread
       * than the ones below.  if we used the destructor, since the threads
       * have a reference to this object, we could wind up trying to wait
       * for ourself to exit, which is a deadlock.
       */
    if (mPreviewThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable or running.  signal it so it wakes
         * up and can exit.
         */
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
    }
    if (mAutoFocusThread != NULL) {
        /* this thread is normally already in it's threadLoop but blocked
         * on the condition variable.  signal it so it wakes up and can exit.
         */
        mFocusLock.lock();
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mFocusCondition.signal();
        mFocusLock.unlock();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
    }
    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }

    if (mRawHeap) {
        mRawHeap->release(mRawHeap);
        mRawHeap = 0;
    }
    if (mPreviewHeap) {
        mPreviewHeap->release(mPreviewHeap);
        mPreviewHeap = 0;
    }

    mV4L2Camera->DeinitCamera();

    mV4L2Camera = NULL;
}

bool CameraHardwareSam::YUY2toYV12(void *srcBuf, void *dstBuf, uint32_t srcWidth, uint32_t srcHeight)
{
    int32_t        x, y, src_y_start_pos, dst_cbcr_pos, dst_pos, src_pos;
    unsigned char *srcBufPointer = (unsigned char *)srcBuf;
    unsigned char *dstBufPointer = (unsigned char *)dstBuf;

    dst_pos = 0;
    dst_cbcr_pos = srcWidth*srcHeight;
    for (uint32_t y = 0; y < srcHeight; y++) {
        src_y_start_pos = (y * (srcWidth * 2));

        for (uint32_t x = 0; x < (srcWidth * 2); x += 2) {
            src_pos = src_y_start_pos + x;

            dstBufPointer[dst_pos++] = srcBufPointer[src_pos];
        }
    }
    for (uint32_t y = 0; y < srcHeight; y += 2) {
        src_y_start_pos = (y * (srcWidth * 2));

        for (uint32_t x = 0; x < (srcWidth * 2); x += 4) {
            src_pos = src_y_start_pos + x;
            dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 1];
        }
    }
    for (uint32_t y = 0; y < srcHeight; y += 2) {
        src_y_start_pos = (y * (srcWidth * 2));

        for (uint32_t x = 0; x < (srcWidth * 2); x += 4) {
            src_pos = src_y_start_pos + x;
            dstBufPointer[dst_cbcr_pos++] = srcBufPointer[src_pos + 3];
        }
    }

    return true;
}
static CameraInfo sCameraInfo[] = {
    {
        CAMERA_FACING_BACK,
        0,  /* orientation */
    },
};

/** Close this device */

static camera_device_t *g_cam_device;

static int HAL_camera_device_close(struct hw_device_t* device)
{
    ALOGI("%s", __func__);
    if (device) {
        camera_device_t *cam_device = (camera_device_t *)device;
        delete static_cast<CameraHardwareSam *>(cam_device->priv);
        free(cam_device);
        g_cam_device = 0;
    }
    return 0;
}

static inline CameraHardwareSam *obj(struct camera_device *dev)
{
    return reinterpret_cast<CameraHardwareSam *>(dev->priv);
}

/** Set the preview_stream_ops to which preview frames are sent */
static int HAL_camera_device_set_preview_window(struct camera_device *dev,
        struct preview_stream_ops *buf)
{
    ALOGD("%s", __func__);
    return obj(dev)->setPreviewWindow(buf);
}

/** Set the notification and data callbacks */
static void HAL_camera_device_set_callbacks(struct camera_device *dev,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void* user)
{
    ALOGD("%s", __func__);
    obj(dev)->setCallbacks(notify_cb, data_cb, data_cb_timestamp,
                           get_memory,
                           user);
}

/**
 * The following three functions all take a msg_type, which is a bitmask of
 * the messages defined in include/ui/Camera.h
 */

/**
 * Enable a message, or set of messages.
 */
static void HAL_camera_device_enable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGD("%s", __func__);
    obj(dev)->enableMsgType(msg_type);
}

/**
 * Disable a message, or a set of messages.
 *
 * Once received a call to disableMsgType(CAMERA_MSG_VIDEO_FRAME), camera
 * HAL should not rely on its client to call releaseRecordingFrame() to
 * release video recording frames sent out by the cameral HAL before and
 * after the disableMsgType(CAMERA_MSG_VIDEO_FRAME) call. Camera HAL
 * clients must not modify/access any video recording frame after calling
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME).
 */
static void HAL_camera_device_disable_msg_type(struct camera_device *dev, int32_t msg_type)
{
    ALOGV("%s", __func__);
    obj(dev)->disableMsgType(msg_type);
}

/**
 * Query whether a message, or a set of messages, is enabled.  Note that
 * this is operates as an AND, if any of the messages queried are off, this
 * will return false.
 */
static int HAL_camera_device_msg_type_enabled(struct camera_device *dev, int32_t msg_type)
{
    ALOGV("%s", __func__);
    return obj(dev)->msgTypeEnabled(msg_type);
}

/**
 * Start preview mode.
 */
static int HAL_camera_device_start_preview(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->startPreview();
}

/**
 * Stop a previously started preview.
 */
static void HAL_camera_device_stop_preview(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    obj(dev)->stopPreview();
}

/**
 * Returns true if preview is enabled.
 */
static int HAL_camera_device_preview_enabled(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->previewEnabled();
}

/**
 * Request the camera HAL to store meta data or real YUV data in the video
 * buffers sent out via CAMERA_MSG_VIDEO_FRAME for a recording session. If
 * it is not called, the default camera HAL behavior is to store real YUV
 * data in the video buffers.
 *
 * This method should be called before startRecording() in order to be
 * effective.
 *
 * If meta data is stored in the video buffers, it is up to the receiver of
 * the video buffers to interpret the contents and to find the actual frame
 * data with the help of the meta data in the buffer. How this is done is
 * outside of the scope of this method.
 *
 * Some camera HALs may not support storing meta data in the video buffers,
 * but all camera HALs should support storing real YUV data in the video
 * buffers. If the camera HAL does not support storing the meta data in the
 * video buffers when it is requested to do do, INVALID_OPERATION must be
 * returned. It is very useful for the camera HAL to pass meta data rather
 * than the actual frame data directly to the video encoder, since the
 * amount of the uncompressed frame data can be very large if video size is
 * large.
 *
 * @param enable if true to instruct the camera HAL to store
 *      meta data in the video buffers; false to instruct
 *      the camera HAL to store real YUV data in the video
 *      buffers.
 *
 * @return OK on success.
 */
static int HAL_camera_device_store_meta_data_in_buffers(struct camera_device *dev, int enable)
{
    ALOGV("%s", __func__);
    return obj(dev)->storeMetaDataInBuffers(enable);
}

/**
 * Start record mode. When a record image is available, a
 * CAMERA_MSG_VIDEO_FRAME message is sent with the corresponding
 * frame. Every record frame must be released by a camera HAL client via
 * releaseRecordingFrame() before the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME). After the client calls
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames,
 * and the client must not modify/access any video recording frames.
 */
static int HAL_camera_device_start_recording(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->startRecording();
}

/**
 * Stop a previously started recording.
 */
static void HAL_camera_device_stop_recording(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    obj(dev)->stopRecording();
}

/**
 * Returns true if recording is enabled.
 */
static int HAL_camera_device_recording_enabled(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->recordingEnabled();
}

/**
 * Release a record frame previously returned by CAMERA_MSG_VIDEO_FRAME.
 *
 * It is camera HAL client's responsibility to release video recording
 * frames sent out by the camera HAL before the camera HAL receives a call
 * to disableMsgType(CAMERA_MSG_VIDEO_FRAME). After it receives the call to
 * disableMsgType(CAMERA_MSG_VIDEO_FRAME), it is the camera HAL's
 * responsibility to manage the life-cycle of the video recording frames.
 */
static void HAL_camera_device_release_recording_frame(struct camera_device *dev,
        const void *opaque)
{
    ALOGV("%s", __func__);
    obj(dev)->releaseRecordingFrame(opaque);
}

/**
 * Start auto focus, the notification callback routine is called with
 * CAMERA_MSG_FOCUS once when focusing is complete. autoFocus() will be
 * called again if another auto focus is needed.
 */
static int HAL_camera_device_auto_focus(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->autoFocus();
}

/**
 * Cancels auto-focus function. If the auto-focus is still in progress,
 * this function will cancel it. Whether the auto-focus is in progress or
 * not, this function will return the focus position to the default.  If
 * the camera does not support auto-focus, this is a no-op.
 */
static int HAL_camera_device_cancel_auto_focus(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->cancelAutoFocus();
}

/**
 * Take a picture.
 */
static int HAL_camera_device_take_picture(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->takePicture();
}

/**
 * Cancel a picture that was started with takePicture. Calling this method
 * when no picture is being taken is a no-op.
 */
static int HAL_camera_device_cancel_picture(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    return obj(dev)->cancelPicture();
}

/**
 * Set the camera parameters. This returns BAD_VALUE if any parameter is
 * invalid or not supported.
 */
static int HAL_camera_device_set_parameters(struct camera_device *dev,
        const char *parms)
{
    ALOGV("%s", __func__);
    String8 str(parms);
    CameraParameters p(str);
    return obj(dev)->setParameters(p);
}

/** Return the camera parameters. */
char *HAL_camera_device_get_parameters(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    String8 str;
    CameraParameters parms = obj(dev)->getParameters();
    str = parms.flatten();
    return strdup(str.string());
}

void HAL_camera_device_put_parameters(struct camera_device *dev, char *parms)
{
    ALOGV("%s", __func__);
    free(parms);
}

/**
 * Send command to camera driver.
 */
static int HAL_camera_device_send_command(struct camera_device *dev,
        int32_t cmd, int32_t arg1, int32_t arg2)
{
    ALOGV("%s", __func__);
    return obj(dev)->sendCommand(cmd, arg1, arg2);
}

/**
 * Release the hardware resources owned by this object.  Note that this is
 * *not* done in the destructor.
 */
static void HAL_camera_device_release(struct camera_device *dev)
{
    ALOGV("%s", __func__);
    obj(dev)->release();
}

/**
 * Dump state of the camera hardware
 */
static int HAL_camera_device_dump(struct camera_device *dev, int fd)
{
    ALOGV("%s", __func__);
    return obj(dev)->dump(fd);
}

static int HAL_getNumberOfCameras()
{
    ALOGV("%s", __func__);
    return sizeof(sCameraInfo) / sizeof(sCameraInfo[0]);
}

static int HAL_getCameraInfo(int cameraId, struct camera_info *cameraInfo)
{
    ALOGV("%s", __func__);
    memcpy(cameraInfo, &sCameraInfo[cameraId], sizeof(CameraInfo));
    return 0;
}

#define SET_METHOD(m) m : HAL_camera_device_##m

static camera_device_ops_t camera_device_ops = {
    SET_METHOD(set_preview_window),
    SET_METHOD(set_callbacks),
    SET_METHOD(enable_msg_type),
    SET_METHOD(disable_msg_type),
    SET_METHOD(msg_type_enabled),
    SET_METHOD(start_preview),
    SET_METHOD(stop_preview),
    SET_METHOD(preview_enabled),
    SET_METHOD(store_meta_data_in_buffers),
    SET_METHOD(start_recording),
    SET_METHOD(stop_recording),
    SET_METHOD(recording_enabled),
    SET_METHOD(release_recording_frame),
    SET_METHOD(auto_focus),
    SET_METHOD(cancel_auto_focus),
    SET_METHOD(take_picture),
    SET_METHOD(cancel_picture),
    SET_METHOD(set_parameters),
    SET_METHOD(get_parameters),
    SET_METHOD(put_parameters),
    SET_METHOD(send_command),
    SET_METHOD(release),
    SET_METHOD(dump),
};

#undef SET_METHOD

static int HAL_camera_device_open(const struct hw_module_t* module,
                                  const char *id,
                                  struct hw_device_t** device)
{
    ALOGV("%s", __func__);

    int cameraId = atoi(id);
    if (cameraId < 0 || cameraId >= HAL_getNumberOfCameras()) {
        ALOGE("Invalid camera ID %s", id);
        return -EINVAL;
    }

    if (g_cam_device) {
        if (obj(g_cam_device)->getCameraId() == cameraId) {
            ALOGV("returning existing camera ID %s", id);
            goto done;
        } else {
            ALOGE("Cannot open camera %d. camera %d is already running!",
                 cameraId, obj(g_cam_device)->getCameraId());
            return -ENOSYS;
        }
    }

    g_cam_device = (camera_device_t *)malloc(sizeof(camera_device_t));
    if (!g_cam_device)
        return -ENOMEM;

    g_cam_device->common.tag     = HARDWARE_DEVICE_TAG;
    g_cam_device->common.version = 1;
    g_cam_device->common.module  = const_cast<hw_module_t *>(module);
    g_cam_device->common.close   = HAL_camera_device_close;

    g_cam_device->ops = &camera_device_ops;

    ALOGI("%s: open camera %s", __func__, id);

    g_cam_device->priv = new CameraHardwareSam(cameraId, g_cam_device);

done:
    *device = (hw_device_t *)g_cam_device;
    ALOGI("%s: opened camera %s (%p)", __func__, id, *device);
    return 0;
}

static hw_module_methods_t camera_module_methods = {
    open : HAL_camera_device_open
};

extern "C" {
    struct camera_module HAL_MODULE_INFO_SYM = {
    common :
        {
            tag: HARDWARE_MODULE_TAG,
            version_major : 1,
            version_minor : 0,
            id: CAMERA_HARDWARE_MODULE_ID,
            name: "Sama5d3 camera HAL",
            author: "Atmel Corporation",
            methods:&camera_module_methods,
        },
        get_number_of_cameras: HAL_getNumberOfCameras,
        get_camera_info: HAL_getCameraInfo
    };
}

}; // namespace android

