/*
 * Copyright (C) 2010 ARM Limited. All rights reserved.
 *
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

#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <linux/fb.h>
#include <sys/types.h>
#include <unistd.h>

#include <hardware/gralloc.h>
#include <cutils/native_handle.h>
#include <alloc_device.h>
#include <utils/Log.h>

/* use by MALI EGL */
#define GRALLOC_ARM_DMA_BUF_MODULE 1

/* the max string size of GRALLOC_HARDWARE_GPU0 & GRALLOC_HARDWARE_FB0
 * 8 is big enough for "gpu0" & "fb0" currently
 */
#define MALI_GRALLOC_HARDWARE_MAX_STR_LEN 8
#define NUM_FB_BUFFERS 2

typedef enum
{
	MALI_YUV_NO_INFO,
	MALI_YUV_BT601_NARROW,
	MALI_YUV_BT601_WIDE,
	MALI_YUV_BT709_NARROW,
	MALI_YUV_BT709_WIDE,
} mali_gralloc_yuv_info;

struct private_handle_t;

struct private_module_t
{
	gralloc_module_t base;

	private_handle_t *framebuffer;
	uint32_t flags;
	uint32_t numBuffers;
	uint32_t bufferMask;
	pthread_mutex_t lock;
	buffer_handle_t currentBuffer;
	int ion_client;
	int drm_fd;

	struct fb_var_screeninfo info;
	struct fb_fix_screeninfo finfo;
	float xdpi;
	float ydpi;
	float fps;

	enum
	{
		// flag to indicate we'll post this buffer
		PRIV_USAGE_LOCKED_FOR_POST = 0x80000000
	};

	/* default constructor */
	private_module_t();
};

#ifdef __cplusplus
struct private_handle_t : public native_handle
{
#else
struct private_handle_t
{
	struct native_handle nativeHandle;
#endif

	enum
	{
		/* keep those emun even we don't use them to let MAli
		 * compile
		 * PRIV_FLAGS_USES_ION is used for any type of dmabuf buffer */
		PRIV_FLAGS_FRAMEBUFFER = 0x00000001,
		PRIV_FLAGS_USES_UMP    = 0x00000002,
		PRIV_FLAGS_USES_ION    = 0x00000004,
	};

	enum
	{
		LOCK_STATE_WRITE     =   1 << 31,
		LOCK_STATE_MAPPED    =   1 << 30,
		LOCK_STATE_READ_MASK =   0x3FFFFFFF
	};

	// file-descriptors
	// shared file descriptor for dma_buf sharing
	int     share_fd;

	// ints
	int     magic;
	int     flags;
	int     usage;
	int     size;
	int     width;
	int     height;
	int     format;
	int     stride;
	void    *base;
	int     lockState;
	int     writeOwner;
	int     pid;

	mali_gralloc_yuv_info yuv_info;

	// Following members is for framebuffer only
	int     fd;
	int     offset;

	unsigned int drm_hnd;
	int	plane_id;

#ifdef __cplusplus
	static const int sNumInts = 17;
	static const int sNumFds = 1;
	static const int sMagic = 0x3141592;

	private_handle_t(int flags, int usage, int size, void *base, int lock_state):
		share_fd(-1),
		magic(sMagic),
		flags(flags),
		usage(usage),
		size(size),
		width(0),
		height(0),
		format(0),
		stride(0),
		base(base),
		lockState(lock_state),
		writeOwner(0),
		pid(getpid()),
		yuv_info(MALI_YUV_NO_INFO),
		fd(0),
		offset(0),
		drm_hnd(0),
		plane_id(0)
	{
		version = sizeof(native_handle);
		numFds = sNumFds;
		numInts = sNumInts;
	}

	private_handle_t(int flags, int usage, int size, void *base, int lock_state, int fb_file, int fb_offset):
		share_fd(-1),
		magic(sMagic),
		flags(flags),
		usage(usage),
		size(size),
		width(0),
		height(0),
		format(0),
		stride(0),
		base(base),
		lockState(lock_state),
		writeOwner(0),
		pid(getpid()),
		yuv_info(MALI_YUV_NO_INFO),
		fd(fb_file),
		offset(fb_offset),
		drm_hnd(0),
		plane_id(0)
	{
		version = sizeof(native_handle);
		numFds = sNumFds;
		numInts = sNumInts;
	}

	~private_handle_t()
	{
		magic = 0;
	}

	static int validate(const native_handle *h)
	{
		const private_handle_t *hnd = (const private_handle_t *)h;

		if (!h || h->version != sizeof(native_handle) || h->numInts != sNumInts || h->numFds != sNumFds || hnd->magic != sMagic)
		{
			return -EINVAL;
		}

		return 0;
	}

	static private_handle_t *dynamicCast(const native_handle *in)
	{
		if (validate(in) == 0)
		{
			return (private_handle_t *) in;
		}

		return NULL;
	}
#endif
};

#endif /* GRALLOC_PRIV_H_ */
