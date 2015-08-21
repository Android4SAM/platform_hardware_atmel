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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "alloc_device.h"
#include "gralloc_priv.h"

#include "drm_fourcc.h"
#include "xf86drm.h"
#include "xf86drmMode.h"

#define GRALLOC_ALIGN( value, base ) (((value) + ((base) - 1)) & ~((base) - 1))

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#endif

inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

static int gralloc_map(gralloc_module_t const* module,
        buffer_handle_t handle,
        void** vaddr)
{
    private_handle_t* hnd = (private_handle_t*)handle;
    if (hnd->flags & private_handle_t::PRIV_FLAGS_DMABUFFER) {
        void* mappedAddress = mmap(0, hnd->size,
                PROT_READ | PROT_WRITE, MAP_SHARED, hnd->fd, hnd->busAddress);
        if (mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap from memalloc %s", strerror(errno));
            return -errno;
        }
        hnd->base = (mappedAddress) + hnd->offset;
    } else if (!(hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER)) {
        size_t size = hnd->size;
        void* mappedAddress = mmap(0, size,
                PROT_READ|PROT_WRITE, MAP_SHARED, hnd->fd, 0);
        if (mappedAddress == MAP_FAILED) {
            ALOGE("Could not mmap %s", strerror(errno));
            return -errno;
        }
        hnd->base = (mappedAddress) + hnd->offset;
        //ALOGD("gralloc_map() succeeded fd=%d, off=%d, size=%d, vaddr=%p",
        //        hnd->fd, hnd->offset, hnd->size, mappedAddress);
    }
    *vaddr = (void*)hnd->base;
    return 0;
}

int mapBuffer(gralloc_module_t const* module,
        private_handle_t* hnd)
{
    void* vaddr;
    return gralloc_map(module, hnd, &vaddr);
}

static int gralloc_alloc_dma_coherent_buffer(alloc_device_t* dev,
        size_t size, int usage, buffer_handle_t* pHandle)
{
    int err = 0;
    int fd = -1;

    size = roundUpToPageSize(size);

    fd = open("/dev/memalloc", O_RDWR | O_SYNC);
    if (fd < 0) {
        ALOGE("couldn't create dma coherent buffer (%s)", strerror(-errno));
        err = -errno;
   }

    if (err == 0) {
        private_handle_t* hnd = new private_handle_t( private_handle_t::PRIV_FLAGS_DMABUFFER, size, fd);
        MemallocParams params;
        memset(&params, 0, sizeof(MemallocParams));
        params.size = hnd->size;
        // get linear memory buffer
        ioctl(hnd->fd, MEMALLOC_IOCXGETBUFFER, &params);

        if (params.busAddress == 0) {
            ALOGE("alloc->fd_memalloc failed (size=%d) - INSUFFICIENT_RESOURCES", params.size);
            return -ENOMEM;
        }

        hnd->busAddress = params.busAddress;
        gralloc_module_t* module = reinterpret_cast<gralloc_module_t*>(
                dev->common.module);
        err = mapBuffer(module, hnd);
        if (err == 0) {
            *pHandle = hnd;
        }
    }

    ALOGE_IF(err, "gralloc dma coherent buffer failed err=%s", strerror(-err));

    return err;
}

static int gralloc_alloc_buffer(alloc_device_t *dev, int width, int height, int bpp, int usage, size_t *stride, buffer_handle_t *pHandle)
{
	private_module_t *m = reinterpret_cast<private_module_t *>(dev->common.module);
	struct drm_mode_create_dumb create_arg;
	struct drm_mode_map_dumb map_arg;
	struct drm_mode_destroy_dumb destroy_arg;
	void *cpu_ptr;
	int ret;
	int prime_fd;
	private_handle_t *hnd;

	memset (&create_arg, 0, sizeof (create_arg));
	create_arg.bpp = bpp;
	create_arg.width = width;
	create_arg.height = height;

	ret = drmIoctl (m->drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_arg);
	if (ret) {
		ALOGE("Failed to create DUMB buffer %s", strerror(errno));
		return ret;
	}

	ret = drmPrimeHandleToFD (m->drm_fd, create_arg.handle, DRM_CLOEXEC, &prime_fd);
	if (ret) {
		ALOGE("Failed to get fd for DUMB buffer %s", strerror(errno));
		goto err;
	}

	memset(&map_arg, 0, sizeof map_arg);
	map_arg.handle = create_arg.handle;
	ret = drmIoctl(m->drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map_arg);
	if (ret) {
		ALOGE("Failed to request DUMB buffer MAP %s", strerror(errno));
		goto err;
	}

	cpu_ptr = mmap(0, create_arg.size, PROT_WRITE | PROT_READ, MAP_SHARED, m->drm_fd, map_arg.offset);

	if (cpu_ptr == MAP_FAILED) {
		ALOGE("Failed to mmap DUMB buffer %s", strerror(errno));
		goto err;
	}

	hnd = new private_handle_t(private_handle_t::PRIV_FLAGS_USES_ION, usage, create_arg.size, cpu_ptr, private_handle_t::LOCK_STATE_MAPPED);

	hnd->share_fd = prime_fd;
	hnd->drm_hnd = create_arg.handle;
	hnd->pid = getpid();
	hnd->size = create_arg.size;

	*pHandle = hnd;

	*stride = (create_arg.pitch * 8) / bpp ;
	return 0;

err:
	memset (&destroy_arg, 0, sizeof destroy_arg);
	destroy_arg.handle = create_arg.handle;
	drmIoctl (m->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
	return ret;
}

static int alloc_device_alloc(alloc_device_t *dev, int w, int h, int format, int usage, buffer_handle_t *pHandle, int *pStride)
{
	if (!pHandle || !pStride)
	{
		return -EINVAL;
	}

	size_t size;
	size_t stride = 0;
	int bpp = 0;
	int err = 0;

	switch (format)
	{
		case HAL_PIXEL_FORMAT_RGBA_8888:
		case HAL_PIXEL_FORMAT_RGBX_8888:
		case HAL_PIXEL_FORMAT_BGRA_8888:
			bpp = 32;
			break;

		case HAL_PIXEL_FORMAT_RGB_888:
			bpp = 24;
			break;

		case HAL_PIXEL_FORMAT_RGB_565:
#if PLATFORM_SDK_VERSION < 19
		case HAL_PIXEL_FORMAT_RGBA_5551:
		case HAL_PIXEL_FORMAT_RGBA_4444:
#endif
			bpp = 16;
			break;
		case HAL_PIXEL_FORMAT_YV12:
			bpp = 12;
			break;
		default:
			return -EINVAL;
	}

	w = GRALLOC_ALIGN(w, 8);

	int private_usage = usage & (GRALLOC_USAGE_PRIVATE_0 | GRALLOC_USAGE_PRIVATE_1);

	switch (private_usage)
	{

		case GRALLOC_USAGE_PRIVATE_0:
			err = gralloc_alloc_dma_coherent_buffer(dev, size, usage, pHandle);;
			break;
		default:
			err = gralloc_alloc_buffer(dev, w, h, bpp, usage, &stride, pHandle);
	}

	if (err)
	{
		return err;
	}

	/* match the framebuffer format */
	if (usage & GRALLOC_USAGE_HW_FB)
	{
#ifdef GRALLOC_16_BITS
		format = HAL_PIXEL_FORMAT_RGB_565;
#else
		format = HAL_PIXEL_FORMAT_BGRA_8888;
#endif
	}

	private_handle_t *hnd = (private_handle_t *)*pHandle;

	hnd->width = w;
	hnd->height = h;
	hnd->format = format;
	hnd->stride = stride;
	hnd->format = format;

	*pStride = stride;
	return 0;
}

static int alloc_device_free(alloc_device_t *dev, buffer_handle_t handle)
{
	private_handle_t const *hnd = reinterpret_cast<private_handle_t const *>(handle);

	if (hnd->flags & private_handle_t::PRIV_FLAGS_USES_ION)
	{
		private_module_t *m = reinterpret_cast<private_module_t *>(dev->common.module);
		struct drm_mode_destroy_dumb destroy_arg;

		munmap((void *)hnd->base, hnd->size);

		memset (&destroy_arg, 0, sizeof destroy_arg);
		destroy_arg.handle = hnd->drm_hnd;
		drmIoctl (m->drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_arg);
		close(hnd->share_fd);

		memset((void *)hnd, 0, sizeof(*hnd));
	}

	delete hnd;

	return 0;
}

static int alloc_device_close(struct hw_device_t *device)
{
	alloc_device_t *dev = reinterpret_cast<alloc_device_t *>(device);

	if (dev) {
		delete dev;
	}

	return 0;
}

int alloc_device_open(hw_module_t const *module, const char *name, hw_device_t **device)
{
	alloc_device_t *dev;

	dev = new alloc_device_t;

	if (NULL == dev)
	{
		return -1;
	}

	/* initialize our state here */
	memset(dev, 0, sizeof(*dev));

	/* initialize the procs */
	dev->common.tag = HARDWARE_DEVICE_TAG;
	dev->common.version = 0;
	dev->common.module = const_cast<hw_module_t *>(module);
	dev->common.close = alloc_device_close;
	dev->alloc = alloc_device_alloc;
	dev->free = alloc_device_free;

	*device = &dev->common;

	return 0;
}
