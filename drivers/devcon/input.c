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
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "input.h"
#include "keyboard.h"

/*
 * Input Handling
 */

struct devcon_input {
	struct list_head list;
	struct input_handle handle;
	bool open : 1;
};

static struct input_handler devcon_input_handler;
static size_t devcon_input_users;
static DEFINE_MUTEX(devcon_input_lock);
static LIST_HEAD(devcon_input_handles);
static DEFINE_SPINLOCK(devcon_input_spinlock);
static LIST_HEAD(devcon_input_handlers);

static bool devcon_iops_filter(struct input_handle *handle,
			       unsigned int type,
			       unsigned int code,
			       int value)
{
	struct devcon_input_handler *h;
	struct devcon_keyboard_event event;
	bool suppress = false;

	switch (type) {
	case EV_KEY:
		if (devcon_keyboard_handle(handle->dev, &event, code, value)) {
			spin_lock(&devcon_input_spinlock);
			list_for_each_entry(h, &devcon_input_handlers, list) {
				if (h->event(h, &event)) {
					suppress = true;
					break;
				}
			}
			spin_unlock(&devcon_input_spinlock);
		}
		break;
	}

	return suppress;
}

static bool devcon_iops_match(struct input_handler *handler,
			      struct input_dev *device)
{
	int i;

	if (test_bit(EV_KEY, device->evbit)) {
		for (i = KEY_RESERVED; i < BTN_MISC; i++)
			if (test_bit(i, device->keybit))
				return true;
	}

	return false;
}

static int devcon_iops_connect(struct input_handler *handler,
			       struct input_dev *device,
			       const struct input_device_id *id)
{
	struct devcon_input *input;
	int ret;

	input = kzalloc(sizeof(*input), GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input->handle.dev = input_get_device(device);
	input->handle.name = "devcon";
	input->handle.handler = handler;

	ret = input_register_handle(&input->handle);
	if (ret < 0)
		goto error;

	mutex_lock(&devcon_input_lock);
	list_add_tail(&input->list, &devcon_input_handles);
	if (devcon_input_users > 0) {
		ret = input_open_device(&input->handle);
		if (ret >= 0)
			input->open = true;
	}
	mutex_unlock(&devcon_input_lock);

	return 0;

error:
	input_put_device(input->handle.dev);
	kfree(input);
	return ret;
}

static void devcon_iops_disconnect(struct input_handle *handle)
{
	struct devcon_input *input = container_of(handle, struct devcon_input,
						  handle);

	mutex_lock(&devcon_input_lock);
	if (input->open) {
		input_close_device(&input->handle);
		input->open = false;
	}
	list_del_init(&input->list);
	mutex_unlock(&devcon_input_lock);

	input_unregister_handle(&input->handle);
	input_put_device(input->handle.dev);
	kfree(input);
}

static void devcon_iops_start(struct input_handle *handle)
{
}

static const struct input_device_id devcon_input_id_table[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY), },
	},
	{ },
};

void devcon_input_init_handler(struct devcon_input_handler *handler)
{
	INIT_LIST_HEAD(&handler->list);
	handler->event = NULL;
}

static void devcon_input_open_close(struct devcon_input *input, int diff)
{
	int ret;

	if (diff > 0) {
		if (!input->open) {
			ret = input_open_device(&input->handle);
			if (ret >= 0)
				input->open = true;
		}
	} else if (diff < 0) {
		if (input->open) {
			input_close_device(&input->handle);
			input->open = false;
		}
	}
}

void devcon_input_open(struct devcon_input_handler *h)
{
	struct devcon_input *input;
	unsigned long flags;

	if (WARN_ON(!devcon_input_handler.name))
		return;
	if (WARN_ON(!list_empty(&h->list)))
		return;
	if (WARN_ON(!h->event))
		return;

	spin_lock_irqsave(&devcon_input_spinlock, flags);
	list_add_tail(&h->list, &devcon_input_handlers);
	spin_unlock_irqrestore(&devcon_input_spinlock, flags);

	mutex_lock(&devcon_input_lock);
	if (!devcon_input_users++)
		list_for_each_entry(input, &devcon_input_handles, list)
			devcon_input_open_close(input, 1);
	mutex_unlock(&devcon_input_lock);
}

void devcon_input_close(struct devcon_input_handler *h)
{
	struct devcon_input *input;
	unsigned long flags;

	if (WARN_ON(!devcon_input_handler.name))
		return;
	if (WARN_ON(!devcon_input_users))
		return;

	spin_lock_irqsave(&devcon_input_spinlock, flags);
	list_del_init(&h->list);
	spin_unlock_irqrestore(&devcon_input_spinlock, flags);

	mutex_lock(&devcon_input_lock);
	if (!--devcon_input_users)
		list_for_each_entry(input, &devcon_input_handles, list)
			devcon_input_open_close(input, -1);
	mutex_unlock(&devcon_input_lock);
}

int devcon_input_init(void)
{
	int ret;

	if (WARN_ON(devcon_input_handler.name))
		return -EINVAL;

	devcon_input_handler.name = "devcon";
	devcon_input_handler.filter = devcon_iops_filter;
	devcon_input_handler.match = devcon_iops_match;
	devcon_input_handler.connect = devcon_iops_connect;
	devcon_input_handler.disconnect = devcon_iops_disconnect;
	devcon_input_handler.start = devcon_iops_start;
	devcon_input_handler.id_table = devcon_input_id_table;

	ret = input_register_handler(&devcon_input_handler);
	if (ret < 0)
		goto error;

	return 0;

error:
	memset(&devcon_input_handler, 0, sizeof(devcon_input_handler));
	return ret;
}

void devcon_input_destroy(void)
{
	if (!devcon_input_handler.name)
		return;

	WARN_ON(!list_empty(&devcon_input_handlers));

	input_unregister_handler(&devcon_input_handler);
	WARN_ON(!list_empty(&devcon_input_handles));
	memset(&devcon_input_handler, 0, sizeof(devcon_input_handler));
}
