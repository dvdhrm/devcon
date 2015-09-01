/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_SCREEN_H
#define __DEVCON_SCREEN_H

#include <linux/kernel.h>
#include "page.h"
#include "parser.h"

struct devcon_screen;

/*
 * Screens
 * A devcon_screen object represents the terminal-side of the communication. It
 * connects devcon_parser and devcon_page and handles all required commands.
 * All runtime state is managed by it.
 */

typedef int (*devcon_screen_write_fn) (struct devcon_screen *screen,
				       void *userdata,
				       const void *buf,
				       size_t size);
typedef int (*devcon_screen_cmd_fn) (struct devcon_screen *screen,
				     void *userdata,
				     unsigned int cmd,
				     const struct devcon_seq *seq);

int devcon_screen_new(struct devcon_screen **out,
		      devcon_screen_write_fn write_fn,
		      void *write_fn_data,
		      devcon_screen_cmd_fn cmd_fn,
		      void *cmd_fn_data);
struct devcon_screen *devcon_screen_free(struct devcon_screen *screen);

unsigned int devcon_screen_get_width(struct devcon_screen *screen);
unsigned int devcon_screen_get_height(struct devcon_screen *screen);
u64 devcon_screen_get_age(struct devcon_screen *screen);

int devcon_screen_feed_text(struct devcon_screen *screen,
			    const u8 *in,
			    size_t size);
int devcon_screen_feed_keyboard(struct devcon_screen *screen,
				const u32 *keysyms,
				size_t n_syms,
				u32 ascii,
				const u32 *ucs4,
				unsigned int mods);
int devcon_screen_resize(struct devcon_screen *screen,
			 unsigned int width,
			 unsigned int height);
void devcon_screen_soft_reset(struct devcon_screen *screen);
void devcon_screen_hard_reset(struct devcon_screen *screen);

int devcon_screen_set_answerback(struct devcon_screen *screen,
				 const char *answerback);

int devcon_screen_draw(struct devcon_screen *screen,
		       int (*draw_fn) (struct devcon_screen *screen,
				       void *userdata,
				       unsigned int x,
				       unsigned int y,
				       const struct devcon_attr *attr,
				       const u32 *ch,
				       size_t n_ch,
				       unsigned int ch_width),
		       void *userdata,
		       u64 *fb_age);

#endif /* __DEVCON_SCREEN_H */
