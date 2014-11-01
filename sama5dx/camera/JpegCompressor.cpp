/*
 * Copyright (C) 2011 The Android Open Source Project
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

//#define LOG_NDEBUG 0
#define LOG_TAG "JPEGCompressor"
#include <cutils/log.h>
#include <assert.h>
#include <dlfcn.h>
#include "JpegCompressor.h"

namespace android {

void* JpegCompressor::mDl = NULL;

static void* getSymbol(void* dl, const char* signature) {
    void* res = dlsym(dl, signature);
    assert (res != NULL);

    return res;
}

typedef void (*InitFunc)(JpegSE* stub, int format, int* strides);
typedef void (*CleanupFunc)(JpegSE* stub);
typedef int (*CompressFunc)(JpegSE* stub, const void* image,
        int width, int height, int quality);
typedef void (*GetCompressedImageFunc)(JpegSE* stub, void* buff);
typedef size_t (*GetCompressedSizeFunc)(JpegSE* stub);

JpegCompressor::JpegCompressor(int format)
{
    /*Supprted format: from YuvToJpegEncoder.cpp
         * HAL_PIXEL_FORMAT_YCrCb_420_SP
         * HAL_PIXEL_FORMAT_YCbCr_422_I
         */
    const char dlName[] = "/system/lib/hw/camera.softwareencoder.jpeg.so";
    if (mDl == NULL) {
        mDl = dlopen(dlName, RTLD_NOW);
    }
    assert(mDl != NULL);

    mFormat = format;

    InitFunc f = (InitFunc)getSymbol(mDl, "JpegSE_init");
    (*f)(&mStub, mFormat, mStrides);
}

JpegCompressor::~JpegCompressor()
{
    CleanupFunc f = (CleanupFunc)getSymbol(mDl, "JpegSE_cleanup");
    (*f)(&mStub);
}

/****************************************************************************
 * Public API
 ***************************************************************************/

status_t JpegCompressor::compressRawImage(const void* image,
                                              int width,
                                              int height,
                                              int quality)
{
    mStrides[0] = width * 2;
    mStrides[1] = width;
    CompressFunc f = (CompressFunc)getSymbol(mDl, "JpegSE_compress");
    return (status_t)(*f)(&mStub, image, width, height, quality);
}


size_t JpegCompressor::getCompressedSize()
{
    GetCompressedSizeFunc f = (GetCompressedSizeFunc)getSymbol(mDl,
            "JpegSE_getCompressedSize");
    return (*f)(&mStub);
}

void JpegCompressor::getCompressedImage(void* buff)
{
    GetCompressedImageFunc f = (GetCompressedImageFunc)getSymbol(mDl,
            "JpegSE_getCompressedImage");
    (*f)(&mStub, buff);
}

}; /* namespace android */
