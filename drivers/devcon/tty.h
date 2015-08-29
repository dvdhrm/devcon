/*
 * Copyright (C) 2013-2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_TTY_H
#define __DEVCON_TTY_H

#include <linux/kernel.h>

struct devcon_tty;

int devcon_tty_init(void);
void devcon_tty_destroy(void);

int devcon_tty_new(struct devcon_tty **out);
struct devcon_tty *devcon_tty_ref(struct devcon_tty *tty);
struct devcon_tty *devcon_tty_unref(struct devcon_tty *tty);
int devcon_tty_add(struct devcon_tty *tty);
void devcon_tty_remove(struct devcon_tty *tty);

#endif /* __DEVCON_TTY_H */
