/*
 * Copyright 2009 Google Inc. All Rights Reserved.
 * Author: rschultz@google.com (Rebecca Schultz Zavin)
 */

#ifndef ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_
#define ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_

int __v4l2_querycap(int fd);
int __v4l2_enuminput(int fd, int index);
int __v4l2_s_input(int fd, int index);
int __v4l2_set_fmt(int fd, uint32_t w, uint32_t h, uint32_t fmt);
int __v4l2_enum_fmt(int fd, uint32_t fmt);
int __v4l2_s_parm(int fp, struct v4l2_streamparm *streamparm);
int __v4l2_req_buf(int fd, uint32_t* reqbufnum, v4l2_memory memtype);
int __v4l2_query_buffer(int fd, int index, struct v4l2_buffer *buf);
int __v4l2_map_buf(int fd, int index, void **start, size_t *len);
int __v4l2_unmap_buf(void *start, size_t len);
int __v4l2_stream_on(int fd);
int __v4l2_stream_off(int fd);
int __v4l2_q_buf(int fd, unsigned length, int pPhyCAddr,
                             int buffer, v4l2_memory memtype);
int __v4l2_dq_buf(int fd, int *index, v4l2_memory memtype);

#endif  /* ANDROID_ZOOM_REPO_HARDWARE_SEC_LIBOVERLAY_V4L2_UTILS_H_*/
