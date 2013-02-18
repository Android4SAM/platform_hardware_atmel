#ifndef ANDROID_COPYBIT_H
#define ANDROID_COPYBIT_H

struct mdp_img_atmel{
	uint32_t width;
	uint32_t height;
	uint32_t format;
 	uint32_t offset;
 	int memory_id;
 	 /* base of buffer with image */
 	void        *base;
};

struct mdp_blit_req_atmel{
	 struct mdp_img_atmel src;
 	struct mdp_img_atmel dst;
 	struct mdp_rect src_rect;
 	struct mdp_rect dst_rect;
 	uint32_t alpha;
 	uint32_t transp_mask;
 	uint32_t flags;
};

struct mdp_blit_req_list_atmel {
	uint32_t count;
	struct mdp_blit_req_atmel req[];
};

#endif  // ANDROID_COPYBIT_H

