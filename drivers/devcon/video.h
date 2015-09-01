/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_VIDEO_H
#define __DEVCON_VIDEO_H

#include <linux/kernel.h>
#include <linux/list.h>

struct devcon_display;
struct devcon_video_handler;

struct devcon_video_handler {
	struct list_head list;
	struct list_head dirty;
	void (*draw) (struct devcon_video_handler *,
		      struct devcon_display *);
	u64 position;
};

int devcon_video_init(void);
void devcon_video_destroy(void);

void devcon_video_init_handler(struct devcon_video_handler *handler);
void devcon_video_open(struct devcon_video_handler *handler);
void devcon_video_close(struct devcon_video_handler *handler);
void devcon_video_dirty(struct devcon_video_handler *handler);

void devcon_video_draw_clear(struct devcon_display *d,
			     unsigned int cell_x,
			     unsigned int cell_y,
			     unsigned int width,
			     unsigned int height);
void devcon_video_draw_glyph(struct devcon_display *d,
			     u32 ch,
			     unsigned int cell_x,
			     unsigned int cell_y);

#endif /* __DEVCON_VIDEO_H */
