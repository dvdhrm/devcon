/*
 * Copyright (C) 2013-2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include "tty.h"

static int __init devcon_init(void)
{
	int ret;

	ret = devcon_tty_init();
	if (ret < 0) {
		pr_err("cannot initialize TTY subsystem\n");
		return ret;
	}

	pr_info("loaded\n");
	return 0;
}

static void __exit devcon_exit(void)
{
	devcon_tty_destroy();
	pr_info("unloaded\n");
}

module_init(devcon_init);
module_exit(devcon_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Developer Console");
