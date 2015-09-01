/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <uapi/linux/major.h>
#include "tty.h"

/*
 * TTY Handling
 * This TTY layer provides an abstraction of the linux TTY subsystem. Each
 * terminal window you open needs to be backed by a TTY so user-space can
 * interact with it. Hence, we provide the 'devcon_tty' object which basically
 * represents a single PTY.
 *
 * The Linux TTY layer is inherently based on static object allocations.
 * However, this is really not what we want. Instead, we have similar
 * requirements to PTYs. We want a fresh new independent TTY for each terminal
 * we spawn. But at the same time, we don't want a file system like devpts.
 * So what we do instead, is we rather treat TTY allocation as hotplugging. For
 * each requested TTY, a new device is created, accessible as /dev/devconX.
 * They're *NOT* ordered (unlike /dev/ttyX) and the numbers are meaningless
 * just like for PTYs. User-space should listen to uevents and spawn a shell or
 * getty on each device.
 *
 * The kernel API hides the TTY complexity from the rest of the code base.
 * Instead, an independent "struct devcon_tty" object is provided. It is
 * allocated for each terminal window and can be added to / removed from the
 * system dynamically. If data is written to the TTY, it is forwarded to the
 * other layers, and if you need to write to the TTY, use the given helpers.
 */

#define DEVCON_TTY_MAJOR TTYAUX_MAJOR
#define DEVCON_TTY_MINOR_FIRST 1024
#define DEVCON_TTY_MINOR_N 256

struct devcon_tty {
	struct tty_port port;
	struct tty_struct *tty;
	unsigned int index;
	devcon_tty_write_fn write_fn;
	void *userdata;
	bool added : 1;
	bool removed : 1;
};

static struct tty_driver *devcon_tty_driver;
static DEFINE_IDR(devcon_tty_idr);
static DEFINE_MUTEX(devcon_tty_lock);

static int devcon_tops_install(struct tty_driver *driver,
			       struct tty_struct *t)
{
	struct devcon_tty *tty;
	int ret;

	mutex_lock(&devcon_tty_lock);
	tty = idr_find(&devcon_tty_idr, t->index);
	if (!tty || WARN_ON(!tty->added || tty->removed)) {
		mutex_unlock(&devcon_tty_lock);
		return -ENODEV;
	}
	devcon_tty_ref(tty);
	mutex_unlock(&devcon_tty_lock);

	ret = tty_standard_install(devcon_tty_driver, t);
	if (ret < 0) {
		devcon_tty_unref(tty);
		return ret;
	}

	t->driver_data = tty;
	return 0;
}

static void devcon_tops_cleanup(struct tty_struct *t)
{
	struct devcon_tty *tty = t->driver_data;

	devcon_tty_unref(tty);
}

static int devcon_tops_open(struct tty_struct *t, struct file *file)
{
	struct devcon_tty *tty = t->driver_data;

	return tty_port_open(&tty->port, t, file);
}

static void devcon_tops_close(struct tty_struct *t, struct file *file)
{
	struct devcon_tty *tty = t->driver_data;

	tty_port_close(&tty->port, t, file);
}

static void devcon_tops_hangup(struct tty_struct *t)
{
	struct devcon_tty *tty = t->driver_data;

	tty_port_hangup(&tty->port);
}

static int devcon_tops_resize(struct tty_struct *t, struct winsize *ws)
{
	return -EINVAL;
}

static void devcon_tops_stop(struct tty_struct *t)
{
}

static void devcon_tops_start(struct tty_struct *t)
{
}

static void devcon_tops_throttle(struct tty_struct *t)
{
}

static void devcon_tops_unthrottle(struct tty_struct *t)
{
}

static int devcon_tops_ioctl(struct tty_struct *t,
			     unsigned int cmd,
			     unsigned long arg)
{
	return -ENOIOCTLCMD;
}

#ifdef CONFIG_COMPAT
static long devcon_tops_compat_ioctl(struct tty_struct *t,
				     unsigned int cmd,
				     unsigned long arg)
{
	return devcon_tops_ioctl(t, cmd, arg);
}
#endif

static int devcon_tops_write(struct tty_struct *t,
			     const unsigned char *data,
			     int size)
{
	struct devcon_tty *tty = t->driver_data;

	if (WARN_ON_ONCE(in_interrupt() || in_atomic()))
		return size;
	if (size == 0)
		return 0;

	if (tty->write_fn)
		tty->write_fn(tty, tty->userdata, (const char *)data, size);

	return size;
}

static int devcon_tops_write_room(struct tty_struct *t)
{
	return t->stopped ? 0 : S16_MAX;
}

static int devcon_tops_chars_in_buffer(struct tty_struct *t)
{
	return 0;
}

static const struct tty_operations devcon_tty_ops = {
	.install		= devcon_tops_install,
	.cleanup		= devcon_tops_cleanup,
	.open			= devcon_tops_open,
	.close			= devcon_tops_close,
	.hangup			= devcon_tops_hangup,
	.resize			= devcon_tops_resize,
	.stop			= devcon_tops_stop,
	.start			= devcon_tops_start,
	.throttle		= devcon_tops_throttle,
	.unthrottle		= devcon_tops_unthrottle,
	.ioctl			= devcon_tops_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl		= devcon_tops_compat_ioctl,
#endif
	.write			= devcon_tops_write,
	.write_room		= devcon_tops_write_room,
	.chars_in_buffer	= devcon_tops_chars_in_buffer,
};

static void devcon_pops_destruct(struct tty_port *port)
{
	struct devcon_tty *tty = container_of(port, struct devcon_tty, port);

	WARN_ON(tty->added && !tty->removed);

	mutex_lock(&devcon_tty_lock);
	idr_remove(&devcon_tty_idr, tty->index);
	mutex_unlock(&devcon_tty_lock);

	kfree(tty);
}

static const struct tty_port_operations devcon_tty_port_ops = {
	.destruct		= devcon_pops_destruct,
};

/**
 * devcon_tty_new() - Allocate new TTY
 * @out:	output variable for new TTY reference
 * @write_fn:	callback for incoming data
 * @userdata:	userdata pointer
 *
 * This allocates a fresh new independent TTY object (similar to opening a PTY
 * master). Nobody but the caller owns a reference to it. It is not linked into
 * the system, yet. This needs to be done via devcon_tty_add().
 *
 * Return: 0 on success, negative error code on failure.
 */
int devcon_tty_new(struct devcon_tty **out,
		   devcon_tty_write_fn write_fn,
		   void *userdata)
{
	struct devcon_tty *tty;
	int index;

	tty = kzalloc(sizeof(*tty), GFP_KERNEL);
	if (!tty)
		return -ENOMEM;

	mutex_lock(&devcon_tty_lock);
	index = idr_alloc(&devcon_tty_idr, NULL, 0, 1 + DEVCON_TTY_MINOR_N,
			  GFP_KERNEL);
	mutex_unlock(&devcon_tty_lock);
	if (index < 0) {
		kfree(tty);
		return index;
	}

	tty->index = index;
	tty->write_fn = write_fn;
	tty->userdata = userdata;
	tty_port_init(&tty->port);
	tty->port.ops = &devcon_tty_port_ops;

	*out = tty;
	return 0;
}

/**
 * devcon_tty_ref() - Acquired reference to TTY
 * @tty:	tty to acquire reference to, or NULL
 *
 * Return: @tty is returned
 */
struct devcon_tty *devcon_tty_ref(struct devcon_tty *tty)
{
	if (tty)
		WARN_ON(!tty_port_get(&tty->port));
	return tty;
}

/**
 * devcon_tty_unref() - Drop reference to TTY
 * @tty:	tty to drop reference to, or NULL
 *
 * Return: NULL is returned
 */
struct devcon_tty *devcon_tty_unref(struct devcon_tty *tty)
{
	if (tty)
		tty_port_put(&tty->port);
	return NULL;
}

/**
 * devcon_tty_add() - Link TTY into system
 * @tty:	tty to link into system
 *
 * This links the given TTY into the system. Once this succeeds, user-space can
 * see and access the TTY object. You have to remove the TTY via
 * devcon_tty_remove() before dropping your reference! The caller is
 * responsible for life-time management.
 *
 * You must not call this multiple times, nor can you call this if the TTY was
 * already removed via devcon_tty_remove().
 *
 * Return: 0 on success, negative error code on failure.
 */
int devcon_tty_add(struct devcon_tty *tty)
{
	struct device *dev;

	/*
	 * Register TTY Device
	 * This adds the given tty device to the system so user-space can start
	 * using it. Note that each TTY device needs a unique minor number,
	 * hence we use a global IDA to manage them. The TTY layer is based on
	 * statically assigned numbers, so we need our own IDA (we could access
	 * driver->ttys[] and lock tty_mutex, but that sounds hack'ish).
	 *
	 * The TTY layer manages the character devices by itself. Even though
	 * tty_register_device() returns a device pointer, we never use it
	 * and we *don't* own a reference. Hence, just treat it as error code.
	 *
	 * Note that we maintain a ref-count on the tty-object as long as its
	 * registered. This is just for safety in case the caller drops its
	 * ref-count without removeing the TTY first (which is a *BUG*).
	 */

	if (WARN_ON(tty->added || tty->removed))
		return -EINVAL;

	tty->added = true;

	mutex_lock(&devcon_tty_lock);
	idr_replace(&devcon_tty_idr, tty, tty->index);
	mutex_unlock(&devcon_tty_lock);

	dev = tty_port_register_device(&tty->port, devcon_tty_driver,
				       tty->index, NULL);
	if (IS_ERR(dev)) {
		mutex_lock(&devcon_tty_lock);
		idr_replace(&devcon_tty_idr, NULL, tty->index);
		mutex_unlock(&devcon_tty_lock);

		tty->removed = true;
		return PTR_ERR(dev);
	}

	devcon_tty_ref(tty);
	return 0;
}

/**
 * devcon_tty_remove() - Remove TTY from system
 * @tty:	tty to remove
 *
 * This reverts the effect of devcon_tty_add(). Once called, the device is
 * removed from the system so user-space cannot see nor access it, anymore.
 * However, note that there might still be pending users. They will not be able
 * to do anything useful with the TTY, though. From a callers perspective, the
 * device is dead and no callbacks will be invoked, anymore. The object will
 * still be around until the last reference is dropped, though.
 *
 * It is safe to call this multiple times, even if devcon_tty_add() was never
 * called. However, the caller is responsible for locking!
 */
void devcon_tty_remove(struct devcon_tty *tty)
{
	/*
	 * Unregister TTY Device
	 * This unregisters a TTY device and removes it from the system.
	 * User-space will not be able to open the device, anymore. However,
	 * there might still be pending users. We hangup the TTY, hence, those
	 * users will be notified about the HUP, but there's no guarantee
	 * they'll close the device node. The TTY layer makes sure the device
	 * is dead, however, the objects must remain until the last
	 * file-descriptor to the TTY is closed. Hence, the TTY may remain
	 * allocated until ->shutdown is called.
	 *
	 * This complexity is *hidden* in this devcon_tty abstraction. The
	 * caller can safely assume the device is dead after this call returns.
	 *
	 * Please note that due to the internals of the TTY layer, user-space
	 * can easily drain our MINOR numbers. The TTY layer *requires* us to
	 * keep minor numbers allocated until the last close (instead of
	 * releasing it on vhangup). User-space can just keep stale FDs open
	 * so we will slowly run out of minor numbers if we keep adding and
	 * removing TTYs.
	 */

	if (tty->removed || !tty->added)
		return;

	tty_port_tty_hangup(&tty->port, false);
	tty_unregister_device(devcon_tty_driver, tty->index);

	mutex_lock(&devcon_tty_lock);
	idr_replace(&devcon_tty_idr, NULL, tty->index);
	mutex_unlock(&devcon_tty_lock);

	tty->removed = true;
	devcon_tty_unref(tty);
}

void devcon_tty_write(struct devcon_tty *tty, const u8 *data, size_t size)
{
	if (WARN_ON(tty->removed || !tty->added))
		return;

	for ( ; size > 0; --size)
		tty_insert_flip_char(&tty->port, *data++, 0);
	tty_schedule_flip(&tty->port);
}

/**
 * devcon_tty_init() - Initialize the TTY abstraction
 *
 * This initializes the TTY subsystem and registers the TTY driver. You must
 * call this before using any other devcon_tty_*() functionality.
 *
 * Return: 0 on success, negative error code on failure.
 */
int devcon_tty_init(void)
{
	int ret;

	if (WARN_ON(!IS_ERR_OR_NULL(devcon_tty_driver)))
		return -EALREADY;

	devcon_tty_driver = tty_alloc_driver(DEVCON_TTY_MINOR_N,
					     TTY_DRIVER_REAL_RAW |
					     TTY_DRIVER_DYNAMIC_DEV |
					     TTY_DRIVER_RESET_TERMIOS);
	if (IS_ERR(devcon_tty_driver))
		return PTR_ERR(devcon_tty_driver);

	devcon_tty_driver->driver_name = "devcon";
	devcon_tty_driver->name = "devcon";
	devcon_tty_driver->major = DEVCON_TTY_MAJOR;
	devcon_tty_driver->minor_start = DEVCON_TTY_MINOR_FIRST;
	devcon_tty_driver->type = TTY_DRIVER_TYPE_CONSOLE;
	devcon_tty_driver->init_termios = tty_std_termios;
	devcon_tty_driver->init_termios.c_iflag |= IUTF8;
	tty_set_operations(devcon_tty_driver, &devcon_tty_ops);

	ret = tty_register_driver(devcon_tty_driver);
	if (ret < 0)
		goto error;

	return 0;

error:
	put_tty_driver(devcon_tty_driver);
	devcon_tty_driver = ERR_PTR(ret);
	return ret;
}

/**
 * devcon_tty_destroy() - Cleanup TTY abstraction
 *
 * This cleans up all resources that were allocated via devcon_tty_init() and
 * the other TTY helpers. The TTY driver is inregistered and no TTY helpers
 * shall be used once this returns.
 */
void devcon_tty_destroy(void)
{
	if (IS_ERR_OR_NULL(devcon_tty_driver))
		return;

	tty_unregister_driver(devcon_tty_driver);
	put_tty_driver(devcon_tty_driver);
	devcon_tty_driver = NULL;

	idr_destroy(&devcon_tty_idr);
}
