/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/console.h>
#include <linux/fb.h>
#include <linux/font.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "video.h"

/*
 * Video Handling
 */

struct devcon_display {
	struct list_head list;
	struct list_head schedule;

	struct fb_info *fbinfo;
	const struct font_desc *font;
	unsigned int width;
	unsigned int height;

	bool need_mode : 1;
	bool need_redraw : 1;
	bool suspended : 1;
	bool blanked : 1;
};

static void devcon_video_worker(struct work_struct *work);

static struct notifier_block devcon_video_notifier;
static u64 devcon_video_position_counter;
static bool devcon_video_running;
static DECLARE_WORK(devcon_video_work, devcon_video_worker);
static DEFINE_MUTEX(devcon_video_lock);
static DEFINE_MUTEX(devcon_video_dirty_lock);
static LIST_HEAD(devcon_video_handlers);
static LIST_HEAD(devcon_video_dirty_list);
static LIST_HEAD(devcon_displays);
static LIST_HEAD(devcon_schedule);

static void devcon_display_schedule(struct devcon_display *d)
{
	if (list_empty(&d->schedule))
		list_add(&d->schedule, &devcon_schedule);
	if (list_is_singular(&devcon_schedule) && devcon_video_running)
		schedule_work(&devcon_video_work);
}

static int devcon_display_new(struct devcon_display **out,
			      struct fb_info *fbinfo)
{
	struct devcon_display *d;
	int ret;

	if (!fbinfo->screen_base)
		return -ENODEV;
	if (fbinfo->fix.visual != FB_VISUAL_TRUECOLOR)
		return -ENODEV;
	if (fbinfo->fix.type != FB_TYPE_PACKED_PIXELS)
		return -ENODEV;
	if (fbinfo->pixmap.flags != FB_PIXMAP_DEFAULT ||
	    fbinfo->pixmap.buf_align != 1 ||
	    fbinfo->pixmap.scan_align != 1)
		return -ENODEV;

	d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return -ENOMEM;

	INIT_LIST_HEAD(&d->list);
	INIT_LIST_HEAD(&d->schedule);
	d->fbinfo = fbinfo;

	/*
	 * Our fb-notifier makes sure to drop any displays immediately on
	 * deactivation, hence, we're sure that the module of the fbdev object
	 * is around at all times. However, we still run try_module_get() to
	 * reduce a race on many buggy fbdev drivers regarding hotplug. We
	 * don't cling to that reference, though. We immediately drop it again
	 * after calling into ->open().
	 */
	if (!try_module_get(fbinfo->fbops->owner)) {
		ret = -ENODEV;
		goto error;
	}
	if (fbinfo->fbops->fb_open) {
		ret = fbinfo->fbops->fb_open(fbinfo, 0);
		if (ret) {
			module_put(fbinfo->fbops->owner);
			ret = ret >= 0 ? -EIO : ret;
			goto error;
		}
	}
	module_put(fbinfo->fbops->owner);

	/* link and schedule modeset */
	list_add(&d->list, &devcon_displays);
	d->need_mode = true;
	devcon_display_schedule(d);

	if (out)
		*out = d;
	return 1;

error:
	kfree(d);
	return ret;
}

static struct devcon_display *devcon_display_free(struct devcon_display *d)
{
	if (!d)
		return NULL;

	if (d->fbinfo->fbops->fb_release)
		d->fbinfo->fbops->fb_release(d->fbinfo, 0);

	list_del_init(&d->schedule);
	list_del_init(&d->list);
	kfree(d);
	return NULL;
}

static void devcon_display_clear(struct devcon_display *d)
{
	struct fb_fillrect region = {};

	if (!d->fbinfo->fbops->fb_fillrect)
		return;

	region.color = 0;
	region.dx = 0;
	region.dy = 0;
	region.width = d->fbinfo->var.xres;
	region.height = d->fbinfo->var.yres;
	region.rop = ROP_COPY;

	d->fbinfo->fbops->fb_fillrect(d->fbinfo, &region);
}

static void devcon_display_prepare(struct devcon_display *d)
{
	struct fb_var_screeninfo var;

	if (!d->need_mode)
		return;

	devcon_display_clear(d);

	var = d->fbinfo->var;
	var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	fb_set_var(d->fbinfo, &var);

	d->need_mode = false;
}

static void devcon_display_restore(struct devcon_display *d)
{
	if (d->need_mode)
		return;

	/* TODO: There's no nice way to restore the previous user right now.
	 *       As a workaround, you should switch VTs to force a full
	 *       screen redraw (or modeset) of the compositor. */

	d->need_mode = true;
}

static void devcon_display_recalc(struct devcon_display *d)
{
	unsigned int w, h;

	/* we only support RGB16, RGB24, XRGB32, and permutations */
	if (d->fbinfo->var.bits_per_pixel != 16 &&
	    d->fbinfo->var.bits_per_pixel != 24 &&
	    d->fbinfo->var.bits_per_pixel != 32)
		goto error;
	if (d->fbinfo->var.grayscale != 0)
		goto error;

	/* we only support 8-aligned font widths/heights */
	d->font = get_default_font(d->fbinfo->var.xres,
				   d->fbinfo->var.yres,
				   0x80808080U, 0x80808080U);
	if (!d->font)
		goto error;

	if (d->font->width % 8 || d->font->height % 8) {
		/* we depend on FONT_8x16, so it must be available */
		d->font = find_font("VGA8x16");
		if (WARN_ON(!d->font ||
			    d->font->width % 8 ||
			    d->font->height % 8))
			goto error;
	}

	/*
	 * TODO: Right now, DRM drivers do not set
	 *       d->fbinfo->var.{width,height}, even though they have the
	 *       information at hand. Once this is fixed, we should implement
	 *       integer scaling for high-dpi displays here.
	 */

	w = d->fbinfo->var.xres / d->font->width;
	h = d->fbinfo->var.yres / d->font->height;
	if (!w || !h)
		goto error;

	if (w != d->width || h != d->height) {
		pr_info("resize fb%d: %ux%u\n", d->fbinfo->node, w, h);
		d->width = w;
		d->height = h;
	}

	return;

error:
	d->font = NULL;
	d->width = 0;
	d->height = 0;
	pr_info("fb%d has incompatible video format\n", d->fbinfo->node);
}

static void devcon_display_draw(struct devcon_display *d,
				struct devcon_video_handler *dirty_handler)
{
	struct devcon_video_handler *h;

	/* ignore incompatible devices */
	if (!d->font || !d->width || !d->height)
		return;

	/*
	 * We need to run all video-handlers in the registration-order to
	 * redraw the screen. If @dirty_handler is set, it points to the
	 * earliest renderer that signaled that it has dirty data. Hence, we
	 * can skip all handlers previous to it. If @dirty_handler is NULL,
	 * it means we need to run *all* handlers. This is usually the case
	 * on display hotplug, etc.
	 */
	if (dirty_handler)
		dirty_handler->draw(dirty_handler, d);

	/* sets @h to beginning of the list if @dirty_handler is NULL */
	h = list_prepare_entry(dirty_handler, &devcon_video_handlers, list);

	list_for_each_entry_continue(h, &devcon_video_handlers, list)
		h->draw(h, d);
}

static void devcon_video_dispatch(struct devcon_display *d,
				  struct devcon_video_handler *dirty_handler)
{
	/* *ALWAYS* unconditionally dequeue from scheduler-queue */
	list_del_init(&d->schedule);

	/* ignore suspended or blanked devices; they're requeued once ready */
	if (d->suspended || d->blanked)
		return;

	if (!lock_fb_info(d->fbinfo))
		return;

	/*
	 * If a display was hotplugged, or if it signaled a modeset, we need
	 * to reinitialize it. We always assume the screen is dirty, so we
	 * force a redraw.
	 */
	if (d->need_mode) {
		devcon_display_prepare(d);
		d->need_redraw = true;
	}

	/*
	 * If a display was modified (or marked for redraw for other reasons),
	 * we assume a mode is properly set, but the content is fully off.
	 * Hence, the dimensions are recalculated and a full render pass is
	 * forces, so all content is refreshed.
	 */
	if (d->need_redraw) {
		devcon_display_recalc(d);
		d->need_redraw = false;
		dirty_handler = NULL;
	}

	devcon_display_draw(d, dirty_handler);

	unlock_fb_info(d->fbinfo);
}

static void devcon_video_worker(struct work_struct *work)
{
	struct devcon_video_handler *h, *dirty_handler = NULL;
	struct devcon_display *d;

	console_lock(); /* Eww.. but needed for fbdev operations */
	mutex_lock(&devcon_video_lock);

	if (list_empty(&devcon_video_handlers)) {
		/* If there are no registered handlers, we should release all
		 * graphics access as we have no content to display. */
		if (devcon_video_running) {
			list_for_each_entry(d, &devcon_displays, list)
				devcon_display_restore(d);
			devcon_video_running = false;
		}
	} else {
		mutex_lock(&devcon_video_dirty_lock);
		while ((h = list_first_entry_or_null(&devcon_video_dirty_list,
						struct devcon_video_handler,
						dirty))) {
			list_del_init(&h->dirty);
			if (!dirty_handler ||
			    h->position < dirty_handler->position)
				dirty_handler = h;
		}
		mutex_unlock(&devcon_video_dirty_lock);

		if (dirty_handler)
			list_for_each_entry(d, &devcon_displays, list)
				devcon_video_dispatch(d, dirty_handler);

		while ((d = list_first_entry_or_null(&devcon_schedule,
						     struct devcon_display,
						     schedule))) {
			devcon_video_dispatch(d, NULL);

			/* safety net to avoid life-locks */
			if (WARN_ON(!list_empty(&d->schedule)))
				list_del_init(&d->schedule);
		}

		devcon_video_running = true;
	}

	mutex_unlock(&devcon_video_lock);
	console_unlock();
}

static int devcon_video_add_display(struct fb_info *fbinfo)
{
	int ret;

	ret = devcon_display_new(NULL, fbinfo);
	if (ret < 0 && ret != -ENODEV) /* ignore hotplug races */
		return ret;

	return 0;
}

static int devcon_video_hotplug(unsigned long action,
				struct devcon_display *d,
				struct fb_event *event)
{
	if (action == FB_EVENT_FB_REGISTERED) {
		if (WARN_ON(d))
			return 0;

		return devcon_video_add_display(event->info);
	}

	if (!d)
		return 0;

	switch (action) {
	case FB_EVENT_FB_UNBIND:
	case FB_EVENT_FB_UNREGISTERED:
		devcon_display_free(d);
		break;
	case FB_EVENT_SUSPEND:
		d->suspended = true;
		break;
	case FB_EVENT_RESUME:
		d->suspended = false;
		d->need_redraw = true;
		devcon_display_schedule(d);
		break;
	case FB_EVENT_MODE_CHANGE:
	case FB_EVENT_MODE_CHANGE_ALL:
		d->need_redraw = true;
		devcon_display_schedule(d);
		break;
	case FB_EVENT_BLANK:
		d->blanked = !(*(int *)event->data == FB_BLANK_UNBLANK);
		d->need_redraw = true;
		if (!d->blanked)
			devcon_display_schedule(d);
		break;
	}

	return 0;
}

static int devcon_video_notify(struct notifier_block *notifier,
			       unsigned long action,
			       void *event)
{
	struct fb_event *fbevent = event;
	struct devcon_display *d = NULL, *di;
	int ret;

	mutex_lock(&devcon_video_lock);
	list_for_each_entry(di, &devcon_displays, list) {
		if (fbevent->info == di->fbinfo) {
			d = di;
			break;
		}
	}
	ret = devcon_video_hotplug(action, d, fbevent);
	if (ret < 0)
		pr_err("failed handling video event %lu: %d\n", action, ret);
	mutex_unlock(&devcon_video_lock);

	return 0; /* always let other handlers continue */
}

void devcon_video_init_handler(struct devcon_video_handler *handler)
{
	INIT_LIST_HEAD(&handler->list);
	INIT_LIST_HEAD(&handler->dirty);
	handler->draw = NULL;
	handler->position = 0;
}

void devcon_video_open(struct devcon_video_handler *handler)
{
	if (WARN_ON(!devcon_video_notifier.notifier_call))
		return;
	if (WARN_ON(!list_empty(&handler->list)))
		return;
	if (WARN_ON(!list_empty(&handler->dirty)))
		return;
	if (WARN_ON(!handler->draw))
		return;

	mutex_lock(&devcon_video_lock);

	list_add_tail(&handler->list, &devcon_video_handlers);
	handler->position = ++devcon_video_position_counter;

	mutex_unlock(&devcon_video_lock);
}

void devcon_video_close(struct devcon_video_handler *handler)
{
	if (WARN_ON(!devcon_video_notifier.notifier_call))
		return;
	if (WARN_ON(list_empty(&handler->list)))
		return;

	mutex_lock(&devcon_video_lock);

	mutex_lock(&devcon_video_dirty_lock);
	list_del_init(&handler->dirty);
	mutex_unlock(&devcon_video_dirty_lock);

	list_del_init(&handler->list);
	if (list_empty(&devcon_video_handlers))
		schedule_work(&devcon_video_work);

	mutex_unlock(&devcon_video_lock);
}

void devcon_video_dirty(struct devcon_video_handler *handler)
{
	if (WARN_ON(!devcon_video_notifier.notifier_call))
		return;

	mutex_lock(&devcon_video_dirty_lock);
	if (list_empty(&handler->dirty))
		list_add_tail(&handler->dirty, &devcon_video_dirty_list);
	if (list_is_singular(&devcon_video_dirty_list))
		schedule_work(&devcon_video_work);
	mutex_unlock(&devcon_video_dirty_lock);
}

void devcon_video_draw_clear(struct devcon_display *d,
			     unsigned int cell_x,
			     unsigned int cell_y,
			     unsigned int width,
			     unsigned int height)
{
	struct fb_fillrect region = {};

	if (WARN_ON(d->font->width % 8 || d->font->height % 8))
		return;
	if (!d->fbinfo->fbops->fb_fillrect)
		return;
	if (cell_x >= d->width || cell_y >= d->height)
		return;

	if (cell_x + width > d->width)
		width = d->width - cell_x;
	if (cell_y + height > d->height)
		height = d->height - cell_y;

	region.color = 0;
	region.dx = cell_x * d->font->width;
	region.dy = cell_y * d->font->height;
	region.width = width * d->font->width;
	region.height = height * d->font->height;
	region.rop = ROP_COPY;

	d->fbinfo->fbops->fb_fillrect(d->fbinfo, &region);
}

void devcon_video_draw_glyph(struct devcon_display *d,
			     u32 ch,
			     unsigned int cell_x,
			     unsigned int cell_y)
{
	struct fb_image image = {};
	u32 s_stride, s_size;
	u32 d_stride, d_size;
	const void *s_data;
	void *d_data;

	if (WARN_ON(d->font->width % 8 || d->font->height % 8))
		return;
	if (!d->fbinfo->fbops->fb_imageblit)
		return;
	if (cell_x >= d->width || cell_y >= d->height)
		return;

	if (ch > 255)
		ch = 0;

	/* first we need to copy the glyph into the pixmap */

	s_stride = d->font->width / 8;
	s_size = d->font->height * s_stride;
	s_data = d->font->data + (ch & 0xff) * s_size;

	d_stride = d->font->width / 8;
	d_size = d->font->height * d_stride;
	d_data = fb_get_buffer_offset(d->fbinfo,
				      &d->fbinfo->pixmap,
				      d_size);

	fb_pad_aligned_buffer(d_data,
			      d_stride,
			      (void *)s_data,
			      s_stride,
			      d->font->height);

	/* now blend the pixmap into the framebuffer */

	image.fg_color = 7;
	image.bg_color = 0;
	image.dx = cell_x * d->font->width;
	image.dy = cell_y * d->font->height;
	image.width = d->font->width;
	image.height = d->font->height;
	image.depth = 1;
	image.data = d_data;

	d->fbinfo->fbops->fb_imageblit(d->fbinfo, &image);
}

int devcon_video_init(void)
{
	int ret, i;

	if (WARN_ON(devcon_video_notifier.notifier_call))
		return -EINVAL;

	devcon_video_notifier.notifier_call = devcon_video_notify;
	ret = fb_register_client(&devcon_video_notifier);
	if (ret < 0)
		goto error;

	/*
	 * Now that our fb-notifier is registered, we need to bind to all
	 * existing displays. Unfortunately, this needs access to the fbdev
	 * registration_lock, everything else is racy (as we might run exactly
	 * in between FB_EVENT_FB_UNBIND and FB_EVENT_FB_UNREGISTER, in which
	 * case we *MUST NOT* access the fbinfo). However, this lock is not
	 * exposed, hence, we try to do our best and lock whatever we can get
	 * our hands on.
	 *
	 * TODO: We *really* need to fix fbmem.c to provide an iterator,
	 *       expose the registration lock, or add a flag that tells us a
	 *       framebuffer is unbound.
	 */
	console_lock();
	mutex_lock(&devcon_video_lock);
	for (i = 0; i < FB_MAX; ++i) {
		struct fb_info *fbinfo;

		fbinfo = ACCESS_ONCE(registered_fb[i]);
		if (!fbinfo)
			continue;

		ret = devcon_video_add_display(fbinfo);
		if (ret < 0)
			pr_err("cannot attach to framebuffer %d: %d\n",
			       i, ret);
	}
	mutex_unlock(&devcon_video_lock);
	console_unlock();

	return 0;

error:
	memset(&devcon_video_notifier, 0, sizeof(devcon_video_notifier));
	return ret;
}

void devcon_video_destroy(void)
{
	struct devcon_display *d;

	if (!devcon_video_notifier.notifier_call)
		return;

	WARN_ON(!list_empty(&devcon_video_handlers));

	fb_unregister_client(&devcon_video_notifier);
	cancel_work_sync(&devcon_video_work);
	memset(&devcon_video_notifier, 0, sizeof(devcon_video_notifier));

	/* locking optional, as we run exclusively */
	mutex_lock(&devcon_video_lock);
	while ((d = list_first_entry_or_null(&devcon_displays,
					     struct devcon_display, list)))
		devcon_display_free(d);
	mutex_unlock(&devcon_video_lock);
}
