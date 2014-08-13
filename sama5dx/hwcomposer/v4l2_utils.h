/*
 * Copyright 2009 Google Inc. All Rights Reserved.
 * Author: rschultz@google.com (Rebecca Schultz Zavin)
 */

#ifndef ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_
#define ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_

int __v4l2_overlay_querycap(int fd, char *cardname);
int __v4l2_overlay_req_buf(int fd, uint32_t* reqbufnum, v4l2_memory memtype);
int v4l2_overlay_query_buffer(int fd, int index, struct v4l2_buffer *buf);
int __v4l2_overlay_map_buf(int fd, int index, void **start, size_t *len);
int __v4l2_overlay_unmap_buf(void *start, size_t len);
int __v4l2_overlay_stream_on(int fd);
int __v4l2_overlay_stream_off(int fd);
int __v4l2_overlay_q_buf(int fd, unsigned length, int pPhyCAddr,
                             int buffer, v4l2_memory memtype);
int __v4l2_overlay_dq_buf(int fd, int *index, v4l2_memory memtype);
int __v4l2_overlay_set_output_fmt(int fd, uint32_t w, uint32_t h, uint32_t fmt);
int __v4l2_overlay_set_overlay_fmt(int fd, uint32_t t, uint32_t l, uint32_t w, uint32_t h);
int v4l2_overlay_get_position(int fd, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
int v4l2_overlay_set_flip(int fd, int degree);
int v4l2_overlay_set_rotation(int fd, int degree, int step);

#endif  /* ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_*/
