/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/module.h>
#include <linux/sysrq.h>
#include "input.h"
#include "terminal.h"
#include "tty.h"
#include "video.h"

static void devcon_sysrq_handler(int key)
{
	devcon_terminal_hotkey();
}

static struct sysrq_key_op devcon_sysrq = {
	.handler	= devcon_sysrq_handler,
	.help_msg	= "devcon(g)",
	.action_msg	= "Invoke developer console",
	.enable_mask	= SYSRQ_ENABLE_KEYBOARD,
};

static int __init devcon_init(void)
{
	int ret;

	ret = devcon_tty_init();
	if (ret < 0) {
		pr_err("cannot initialize TTY subsystem\n");
		goto error;
	}

	ret = devcon_input_init();
	if (ret < 0) {
		pr_err("cannot initialize input subsystem\n");
		goto error;
	}

	ret = devcon_video_init();
	if (ret < 0) {
		pr_err("cannot initialize video subsystem\n");
		goto error;
	}

	ret = devcon_terminal_init();
	if (ret < 0) {
		pr_err("cannot initialize terminal subsystem\n");
		goto error;
	}

	ret = register_sysrq_key('g', &devcon_sysrq);
	if (ret < 0) {
		pr_err("cannot register sysrq handler\n");
		goto error;
	}

	pr_info("loaded\n");
	return 0;

error:
	devcon_terminal_destroy();
	devcon_video_destroy();
	devcon_input_destroy();
	devcon_tty_destroy();
	return ret;
}

static void __exit devcon_exit(void)
{
	unregister_sysrq_key('g', &devcon_sysrq);
	devcon_terminal_destroy();
	devcon_video_destroy();
	devcon_input_destroy();
	devcon_tty_destroy();
	pr_info("unloaded\n");
}

module_init(devcon_init);
module_exit(devcon_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Developer Console");
