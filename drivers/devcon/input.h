/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_INPUT_H
#define __DEVCON_INPUT_H

#include <linux/kernel.h>
#include <linux/list.h>
#include "keyboard.h"

struct devcon_input_event;
struct devcon_input_handler;

struct devcon_input_handler {
	struct list_head list;
	bool (*event) (struct devcon_input_handler *,
		       const struct devcon_keyboard_event *);
};

int devcon_input_init(void);
void devcon_input_destroy(void);

void devcon_input_init_handler(struct devcon_input_handler *handler);
void devcon_input_open(struct devcon_input_handler *handler);
void devcon_input_close(struct devcon_input_handler *handler);

#endif /* __DEVCON_INPUT_H */
