/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/input.h>
#include <linux/kernel.h>
#include <uapi/linux/input.h>
#include "keyboard.h"

/*
 * Keyboard Translation
 */

/* trivial translation table for ASCII layout english keyboards */
static const char devcon_keyboard_xlate[] =
	"\000\0331234567890-=\177\t"			/* 0x00 - 0x0f */
	"qwertyuiop[]\r\000as"				/* 0x10 - 0x1f */
	"dfghjkl;'`\000\\zxcv"				/* 0x20 - 0x2f */
	"bnm,./\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"		/* 0x40 - 0x4f */
	"230\177\0\0\213\214\0\0\0\0\0\0\0\0\0\0"	/* 0x50 - 0x5f */
	"\r\000/";					/* 0x60 - 0x6f */
static const char devcon_keyboard_xlate_shift[] =
	"\000\033!@#$%^&*()_+\177\t"			/* 0x00 - 0x0f */
	"QWERTYUIOP{}\r\000AS"				/* 0x10 - 0x1f */
	"DFGHJKL:\"~\000|ZXCV"				/* 0x20 - 0x2f */
	"BNM<>?\000*\000 \000\201\202\203\204\205"	/* 0x30 - 0x3f */
	"\206\207\210\211\212\000\000789-456+1"		/* 0x40 - 0x4f */
	"230\177\0\0\213\214\0\0\0\0\0\0\0\0\0\0"	/* 0x50 - 0x5f */
	"\r\000/";					/* 0x60 - 0x6f */

bool devcon_keyboard_handle(struct input_dev *dev,
			    struct devcon_keyboard_event *event,
			    unsigned int code,
			    int value)
{
	char ch = 0;

	event->mods = 0;
	event->symbol = 0;
	event->ascii = 0;
	event->ucs4 = 0;

	if (value < 1)
		return false;

	if (test_bit(KEY_LEFTSHIFT, dev->key) ||
	    test_bit(KEY_RIGHTSHIFT, dev->key))
		event->mods |= DEVCON_MOD_SHIFT;

	if (test_bit(KEY_LEFTCTRL, dev->key) ||
	    test_bit(KEY_RIGHTCTRL, dev->key))
		event->mods |= DEVCON_MOD_CTRL;

	if (test_bit(KEY_LEFTALT, dev->key) ||
	    test_bit(KEY_RIGHTALT, dev->key))
		event->mods |= DEVCON_MOD_ALT;

	if (test_bit(KEY_LEFTMETA, dev->key) ||
	    test_bit(KEY_RIGHTMETA, dev->key))
		event->mods |= DEVCON_MOD_META;

	if (event->mods & DEVCON_MOD_SHIFT) {
		if (code < sizeof(devcon_keyboard_xlate_shift) - 1)
			ch = devcon_keyboard_xlate_shift[code];
	} else {
		if (code < sizeof(devcon_keyboard_xlate) - 1)
			ch = devcon_keyboard_xlate[code];
	}

	event->symbol = code;
	event->ascii = ch < 128 ? ch : 0;
	event->ucs4 = ch;

	return true;
}
