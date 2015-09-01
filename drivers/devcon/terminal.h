/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_TERMINAL_H
#define __DEVCON_TERMINAL_H

#include <linux/kernel.h>

int devcon_terminal_init(void);
void devcon_terminal_destroy(void);

void devcon_terminal_hotkey(void);

#endif /* __DEVCON_TERMINAL_H */
