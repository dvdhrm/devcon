/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_KEYBOARD_H
#define __DEVCON_KEYBOARD_H

#include <linux/input.h>
#include <linux/kernel.h>

enum {
	DEVCON_MOD_SHIFT	= (1 << 0),
	DEVCON_MOD_CTRL		= (1 << 1),
	DEVCON_MOD_ALT		= (1 << 2),
	DEVCON_MOD_META		= (1 << 3),
};

struct devcon_keyboard_event {
	unsigned int mods;
	u32 symbol;
	u32 ascii;
	u32 ucs4;
};

bool devcon_keyboard_handle(struct input_dev *dev,
			    struct devcon_keyboard_event *event,
			    unsigned int code,
			    int value);

#endif /* __DEVCON_KEYBOARD_H */
