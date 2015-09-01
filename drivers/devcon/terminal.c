/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/atomic.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include "input.h"
#include "screen.h"
#include "terminal.h"
#include "tty.h"
#include "video.h"

/*
 * Terminal Management
 */

struct devcon_window {
	struct devcon_terminal *terminal;
	struct list_head list;
	struct mutex lock;
	struct devcon_input_handler input;
	struct devcon_video_handler video;
	struct devcon_screen *screen;
	struct devcon_tty *tty;

	bool raised : 1;
};

enum {
	_DEVCON_OP_NONE,
	DEVCON_OP_QUIT,
	DEVCON_OP_CLOSE,
	DEVCON_OP_HIDE,
};

struct devcon_terminal {
	struct mutex lock;
	struct devcon_input_handler input;
	struct devcon_video_handler video;
	struct list_head windows;
	struct devcon_window *active;

	atomic_t dead;
	atomic_t operation;

	bool running : 1;
	bool shown : 1;
};

static void devcon_terminal_worker(struct work_struct *work);

static struct devcon_terminal *devcon_terminal_glob;
static DECLARE_WORK(devcon_terminal_work, devcon_terminal_worker);

static int devcon_window_tty_output(struct devcon_screen *screen,
				    void *userdata,
				    const void *data,
				    size_t size)
{
	struct devcon_window *window = userdata;

	/* window is locked */

	devcon_tty_write(window->tty, data, size);
	return 0;
}

static void devcon_window_tty_input(struct devcon_tty *tty,
				    void *userdata,
				    const char *data,
				    size_t size)
{
	struct devcon_window *window = userdata;

	mutex_lock(&window->lock);
	devcon_screen_feed_text(window->screen, (const u8 *)data, size);
	if (window->raised)
		devcon_video_dirty(&window->video);
	mutex_unlock(&window->lock);
}

static bool devcon_window_input(struct devcon_input_handler *input,
				const struct devcon_keyboard_event *event)
{
	struct devcon_window *window = container_of(input,
						    struct devcon_window,
						    input);

	/* atomic context: cannot lock the window */
	if (WARN_ON(!window->raised))
		return false;

	devcon_screen_feed_keyboard(window->screen,
				    &event->symbol,
				    1,
				    event->ascii,
				    &event->ucs4,
				    event->mods);

	return true;
}

static int devcon_window_draw_cell(struct devcon_screen *screen,
				   void *userdata,
				   unsigned int x,
				   unsigned int y,
				   const struct devcon_attr *attr,
				   const u32 *ch,
				   size_t n_ch,
				   unsigned int cwidth)
{
	struct devcon_display *display = userdata;
	unsigned int i;

	devcon_video_draw_glyph(display, ch[0], x, y);
	for (i = 1; i < cwidth; ++i)
		devcon_video_draw_glyph(display, 0, x, y);

	return 0;
}

static void devcon_window_draw(struct devcon_video_handler *video,
			       struct devcon_display *display)
{
	struct devcon_window *window = container_of(video,
						    struct devcon_window,
						    video);

	if (WARN_ON(!window->raised))
		return;

	mutex_lock(&window->lock);
	devcon_screen_draw(window->screen,
			   devcon_window_draw_cell,
			   display,
			   NULL);
	mutex_unlock(&window->lock);
}

static struct devcon_window *devcon_window_free(struct devcon_window *window)
{
	if (!window)
		return NULL;

	WARN_ON(window->raised);
	WARN_ON(window == window->terminal->active);

	list_del(&window->list);
	if (window->tty) {
		devcon_tty_remove(window->tty);
		devcon_tty_unref(window->tty);
	}
	devcon_screen_free(window->screen);
	kfree(window);
	return NULL;
}

static int devcon_window_new(struct devcon_window **out,
			     struct devcon_terminal *t)
{
	struct devcon_window *window;
	int ret;

	window = kzalloc(sizeof(*window), GFP_KERNEL);
	if (!window)
		return -ENOMEM;

	window->terminal = t;
	INIT_LIST_HEAD(&window->list);
	mutex_init(&window->lock);
	devcon_input_init_handler(&window->input);
	window->input.event = devcon_window_input;
	devcon_video_init_handler(&window->video);
	window->video.draw = devcon_window_draw;

	ret = devcon_screen_new(&window->screen,
				devcon_window_tty_output,
				window,
				NULL,
				NULL);
	if (ret < 0)
		goto error;

	ret = devcon_screen_resize(window->screen, 80, 24);
	if (ret < 0)
		goto error;

	ret = devcon_tty_new(&window->tty, devcon_window_tty_input, window);
	if (ret < 0)
		goto error;

	ret = devcon_tty_add(window->tty);
	if (ret < 0)
		goto error;

	list_add_tail(&window->list, &t->windows);

	*out = window;
	return 0;

error:
	devcon_window_free(window);
	return ret;
}

static void devcon_window_raise(struct devcon_window *window)
{
	if (window->raised)
		return;

	mutex_lock(&window->lock);
	window->raised = true;
	devcon_video_open(&window->video);
	devcon_input_open(&window->input);
	mutex_unlock(&window->lock);
}

static void devcon_window_lower(struct devcon_window *window)
{
	if (!window->raised)
		return;

	mutex_lock(&window->lock);
	devcon_input_close(&window->input);
	devcon_video_close(&window->video);
	window->raised = false;
	mutex_unlock(&window->lock);
}

static bool devcon_terminal_input(struct devcon_input_handler *input,
				  const struct devcon_keyboard_event *event)
{
	struct devcon_terminal *t = container_of(input, struct devcon_terminal,
						 input);

	/* atomic context: cannot lock the terminal */

	switch (event->symbol) {
	case KEY_H:
		if (event->mods & DEVCON_MOD_META) {
			atomic_set(&t->operation, DEVCON_OP_HIDE);
			schedule_work(&devcon_terminal_work);
			return true;
		}
		break;
	case KEY_Q:
		if (event->mods & DEVCON_MOD_META) {
			atomic_set(&t->operation, DEVCON_OP_QUIT);
			schedule_work(&devcon_terminal_work);
			return true;
		}
		break;
	}

	return false;
}

static void devcon_terminal_draw(struct devcon_video_handler *video,
				 struct devcon_display *display)
{
	devcon_video_draw_clear(display, 0, 0, -1, -1);
}

static int devcon_terminal_new(struct devcon_terminal **out)
{
	struct devcon_terminal *t;

	t = kzalloc(sizeof(*t), GFP_KERNEL);
	if (!t)
		return -ENOMEM;

	mutex_init(&t->lock);
	devcon_input_init_handler(&t->input);
	t->input.event = devcon_terminal_input;
	devcon_video_init_handler(&t->video);
	t->video.draw = devcon_terminal_draw;
	INIT_LIST_HEAD(&t->windows);
	atomic_set(&t->dead, 0);

	*out = t;
	return 0;
}

static struct devcon_terminal *devcon_terminal_free(struct devcon_terminal *t)
{
	if (!t)
		return NULL;

	WARN_ON(!list_empty(&t->windows));
	WARN_ON(t->active);

	kfree(t);
	return NULL;
}

static void devcon_terminal_show(struct devcon_terminal *t)
{
	if (t->shown || !t->running || atomic_read(&t->dead))
		return;

	pr_info("show terminal\n");
	t->shown = true;

	devcon_video_open(&t->video);
	devcon_video_dirty(&t->video);

	if (t->active)
		devcon_window_raise(t->active);
}

static void devcon_terminal_hide(struct devcon_terminal *t)
{
	if (!t->shown || !t->running)
		return;

	if (t->active)
		devcon_window_lower(t->active);

	devcon_video_close(&t->video);

	t->shown = false;
	pr_info("hide terminal\n");
}

static int devcon_terminal_start(struct devcon_terminal *t)
{
	struct devcon_window *window;
	int ret;

	if (atomic_read(&t->dead))
		return -EBUSY;
	if (t->running)
		return 0;

	if (list_empty(&t->windows)) {
		ret = devcon_window_new(&window, t);
		if (ret < 0)
			return ret;
	} else if (!t->active) {
		window = list_first_entry(&t->windows, struct devcon_window,
					  list);
	} else {
		window = t->active;
	}

	devcon_input_open(&t->input);

	t->running = true;
	t->active = window;

	return 0;
}

static void devcon_terminal_stop(struct devcon_terminal *t, bool kill)
{
	struct devcon_window *window;

	if (kill)
		atomic_set(&t->dead, 1);

	if (!t->running)
		return;

	devcon_terminal_hide(t);
	devcon_input_close(&t->input);
	t->active = NULL;

	while ((window = list_first_entry_or_null(&t->windows,
						  struct devcon_window, list)))
		devcon_window_free(window);

	t->running = false;
}

static void devcon_terminal_worker(struct work_struct *work)
{
	struct devcon_terminal *t = devcon_terminal_glob;
	int ret, op;

	if (WARN_ON(!t))
		return;

	mutex_lock(&t->lock);
	if (atomic_read(&t->dead)) {
		/* Either the terminal is already dead and stopped, or a force
		 * stop was scheduled. Either way, we just make sure the
		 * terminal is stopped (if not already) and no further actions
		 * are invoked. */
		devcon_terminal_stop(t, true);
	} else if ((op = atomic_xchg(&t->operation, 0))) {
		switch (op) {
		case DEVCON_OP_QUIT:
			devcon_terminal_stop(t, false);
			break;
		case DEVCON_OP_HIDE:
			if (t->shown)
				devcon_terminal_hide(t);
			else
				devcon_terminal_show(t);
			break;
		}
	} else if (t->running) {
		if (!t->shown)
			devcon_terminal_show(t);
		else
			/* devcon_terminal_hide(t); */
			devcon_terminal_stop(t, false);
	} else {
		ret = devcon_terminal_start(t);
		if (ret < 0)
			pr_err("hotkey failed: %d\n", ret);
		else
			devcon_terminal_show(t);
	}
	mutex_unlock(&t->lock);
}

/**
 * devcon_terminal_hotkey() - Invoke terminal hotkey handlers
 *
 * This needs to be called whenever the global hotkey is pressed. It will
 * schedule a handler will performs the configured hotkey actions. Note that we
 * only support a single global hotkey, hence, there's no need to tell which
 * hotkey was invoked.
 *
 * The caller is responsible to call this only if the terminal layer is active.
 * Hence, if you call devcon_terminal_destroy(), you must not invoke the hotkey
 * handler, anymore.
 *
 * This can be called from atomic context just fine.
 */
void devcon_terminal_hotkey(void)
{
	schedule_work(&devcon_terminal_work);
}

/**
 * devcon_terminal_init() - Initialize the terminal layer
 *
 * This prepares the terminal layer and allocates required resources. The
 * initial terminal state tracking is set up, but it is not activated. Use the
 * hotkey handler devcon_terminal_hotkey() to invoke the terminal.
 */
int devcon_terminal_init(void)
{
	int ret;

	if (WARN_ON(devcon_terminal_glob))
		return -EINVAL;

	ret = devcon_terminal_new(&devcon_terminal_glob);
	if (ret < 0)
		return ret;

	return 0;
}

/**
 * devcon_terminal_destroy() - Cleanup the terminal layer
 *
 * This reverts the effect of devcon_terminal_init() and destroys the terminal
 * layer. If the terminal is active, it's deactivated and disabled. Any
 * allocated resources are then released.
 */
void devcon_terminal_destroy(void)
{
	struct devcon_terminal *t = devcon_terminal_glob;

	if (!t)
		return;

	/*
	 * We force-stop the terminal first. It is then marked as dead and will
	 * not schedule any operations, anymore. Any further call to
	 * devcon_terminal_start() will be rejected.
	 * We then synchronously wait for a possible worker to finish (the
	 * worker will not have any effect as the terminal is marked as dead)
	 * before destroying the terminal resources.
	 */

	mutex_lock(&t->lock);
	devcon_terminal_stop(t, true);
	mutex_unlock(&t->lock);

	cancel_work_sync(&devcon_terminal_work);
	devcon_terminal_glob = devcon_terminal_free(t);
}
