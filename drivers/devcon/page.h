/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_PAGE_H
#define __DEVCON_PAGE_H

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>

struct devcon_attr;
struct devcon_char;
struct devcon_charbuf;
struct devcon_color;
struct devcon_cell;
struct devcon_history;
struct devcon_line;
struct devcon_page;

/*
 * Miscellaneous
 * Ranndom helpers not specific to one of our objects.
 */

#define DEVCON_AGE_NULL (0ULL)

int mk_wcwidth(int ucs4);

/*
 * Characters
 * Each cell in a terminal page contains only a single character. This is
 * usually a single UCS-4 value. However, Unicode allows combining-characters,
 * therefore, the number of UCS-4 characters per cell must be unlimited. The
 * devcon_char object wraps the internal combining char API so it can be
 * treated as a single object.
 */

struct devcon_char {
	/* never access this value directly */
	u64 _value;
};

struct devcon_charbuf {
	/* 3 characters + zero-terminator */
	u32 buf[4];
};

#define DEVCON_CHAR_INIT(_val) ((struct devcon_char){ ._value = (_val) })
#define DEVCON_CHAR_NULL DEVCON_CHAR_INIT(0)

struct devcon_char devcon_char_set(struct devcon_char previous,
				   u32 append_ucs4);
struct devcon_char devcon_char_merge(struct devcon_char base,
				     u32 append_ucs4);
struct devcon_char devcon_char_dup(struct devcon_char ch);

const u32 *devcon_char_resolve(struct devcon_char ch,
			       size_t *s,
			       struct devcon_charbuf *b);
unsigned int devcon_char_lookup_width(struct devcon_char ch);

/* true if @ch is DEVCON_CHAR_NULL, otherwise false */
static inline bool devcon_char_is_null(struct devcon_char ch)
{
	return ch._value == 0;
}

/* true if @ch is dynamically allocated and needs to be freed */
static inline bool devcon_char_is_allocated(struct devcon_char ch)
{
	return !devcon_char_is_null(ch) && !(ch._value & 0x1);
}

/* true if (a == b), otherwise false; this is (a == b), NOT (*a == *b) */
static inline bool devcon_char_same(struct devcon_char a, struct devcon_char b)
{
	return a._value == b._value;
}

/* true if (*a == *b), otherwise false; this is implied by (a == b) */
static inline bool devcon_char_equal(struct devcon_char a, struct devcon_char b)
{
	struct devcon_charbuf ca, cb;
	const u32 *sa, *sb;
	size_t na, nb;

	sa = devcon_char_resolve(a, &na, &ca);
	sb = devcon_char_resolve(b, &nb, &cb);
	return na == nb && !memcmp(sa, sb, sizeof(*sa) * na);
}

/*
 * Attributes
 * The devcon_attr structure describes screen attributes of a terminal cell
 * that can be modified by the client application. Storage management of the
 * object is done by the caller, and the object can be copied by value.
 */

enum {
	/* special color-codes */
	DEVCON_CCODE_DEFAULT,	/* default foreground/background color */
	DEVCON_CCODE_256,	/* 256color code */
	DEVCON_CCODE_RGB,	/* color is specified as RGB */

	/* dark color-codes */
	DEVCON_CCODE_BLACK,
	DEVCON_CCODE_RED,
	DEVCON_CCODE_GREEN,
	DEVCON_CCODE_YELLOW,
	DEVCON_CCODE_BLUE,
	DEVCON_CCODE_MAGENTA,
	DEVCON_CCODE_CYAN,
	DEVCON_CCODE_WHITE,

	/* light color-codes */
	DEVCON_CCODE_LIGHT_BLACK	= DEVCON_CCODE_BLACK + 8,
	DEVCON_CCODE_LIGHT_RED		= DEVCON_CCODE_RED + 8,
	DEVCON_CCODE_LIGHT_GREEN	= DEVCON_CCODE_GREEN + 8,
	DEVCON_CCODE_LIGHT_YELLOW	= DEVCON_CCODE_YELLOW + 8,
	DEVCON_CCODE_LIGHT_BLUE		= DEVCON_CCODE_BLUE + 8,
	DEVCON_CCODE_LIGHT_MAGENTA	= DEVCON_CCODE_MAGENTA + 8,
	DEVCON_CCODE_LIGHT_CYAN		= DEVCON_CCODE_CYAN + 8,
	DEVCON_CCODE_LIGHT_WHITE	= DEVCON_CCODE_WHITE + 8,

	DEVCON_CCODE_N,
};

struct devcon_color {
	u8 ccode;
	union {
		u8 c256;
		struct {
			u8 red;
			u8 green;
			u8 blue;
		};
	};
};

struct devcon_attr {
	/* make sure that zero(*attr) is the default! */
	struct devcon_color fg;		/* foreground color */
	struct devcon_color bg;		/* background color */

	unsigned int bold : 1;		/* bold font */
	unsigned int italic : 1;	/* italic font */
	unsigned int underline : 1;	/* underline text */
	unsigned int inverse : 1;	/* inverse fg/bg */
	unsigned int protect : 1;	/* protect from erase */
	unsigned int blink : 1;		/* blink text */
	unsigned int hidden : 1;	/* hidden */
};

void devcon_attr_to_argb32(const struct devcon_attr *attr,
			   u32 *fg, u32 *bg, const u8 *palette);

/*
 * Cells
 * The devcon_cell structure respresents a single cell in a terminal page. It
 * contains the stored character, the age of the cell and all its attributes.
 */

struct devcon_cell {
	struct devcon_char ch;		/* stored char or DEVCON_CHAR_NULL */
	u64 age;			/* cell age or DEVCON_AGE_NULL */
	struct devcon_attr attr;	/* cell attributes */
	unsigned int cwidth;		/* cached wcwidth(ch) */
};

/*
 * Lines
 * Instead of storing cells in a 2D array, we store them in an array of
 * dynamically allocated lines. This way, scrolling can be implemented very
 * fast without moving any cells at all. Similarly, the scrollback-buffer is
 * much simpler to implement.
 * We use struct devcon_line to store a single line. It contains an array of
 * cells, a fill-state which remembers the amount of blanks on the right side,
 * a separate age just for the line which can overwrite the age for all cells,
 * and some management data.
 */

struct devcon_line {
	struct list_head list;		/* linked list for history buffer */

	unsigned int width;		/* visible width of line */
	unsigned int n_cells;		/* # of allocated cells */
	struct devcon_cell *cells;	/* cell-array */

	u64 age;			/* line age */
	unsigned int fill;		/* # of valid cells; starting left */
};

/*
 * Pages
 * A page represents the 2D table containing all cells of a terminal. It stores
 * lines as an array of pointers so scrolling becomes a simple line-shuffle
 * operation.
 * Scrolling is always targeted only at the scroll-region defined via scroll_idx
 * and scroll_num. The fill-state keeps track of the number of touched lines in
 * the scroll-region. @width and @height describe the visible region of the page
 * and are guaranteed to be allocated at all times.
 */

struct devcon_page {
	u64 age;			/* page age */

	struct devcon_line **lines;	/* array of line-pointers */
	struct devcon_line **line_cache;/* cache for temporary operations */
	unsigned int n_lines;		/* # of allocated lines */

	unsigned int width;		/* width of visible area */
	unsigned int height;		/* height of visible area */
	unsigned int scroll_idx;	/* scrolling-region start index */
	unsigned int scroll_num;	/* scrolling-region length in lines */
	unsigned int scroll_fill;	/* # of valid scroll-lines */
};

int devcon_page_new(struct devcon_page **out);
struct devcon_page *devcon_page_free(struct devcon_page *page);

struct devcon_cell *devcon_page_get_cell(struct devcon_page *page,
					 unsigned int x,
					 unsigned int y);

int devcon_page_reserve(struct devcon_page *page,
			unsigned int cols,
			unsigned int rows,
			const struct devcon_attr *attr,
			u64 age);
void devcon_page_resize(struct devcon_page *page,
			unsigned int cols,
			unsigned int rows,
			const struct devcon_attr *attr,
			u64 age,
			struct devcon_history *history);
void devcon_page_write(struct devcon_page *page,
		       unsigned int pos_x,
		       unsigned int pos_y,
		       struct devcon_char ch,
		       unsigned int cwidth,
		       const struct devcon_attr *attr,
		       u64 age,
		       bool insert_mode);
void devcon_page_insert_cells(struct devcon_page *page,
			      unsigned int from_x,
			      unsigned int from_y,
			      unsigned int num,
			      const struct devcon_attr *attr,
			      u64 age);
void devcon_page_delete_cells(struct devcon_page *page,
			      unsigned int from_x,
			      unsigned int from_y,
			      unsigned int num,
			      const struct devcon_attr *attr,
			      u64 age);
void devcon_page_append(struct devcon_page *page,
			unsigned int pos_x,
			unsigned int pos_y,
			u32 ucs4,
			u64 age);
void devcon_page_erase(struct devcon_page *page,
		       unsigned int from_x,
		       unsigned int from_y,
		       unsigned int to_x,
		       unsigned int to_y,
		       const struct devcon_attr *attr,
		       u64 age,
		       bool keep_protected);
void devcon_page_reset(struct devcon_page *page,
		       const struct devcon_attr *attr,
		       u64 age);

void devcon_page_set_scroll_region(struct devcon_page *page,
				   unsigned int idx,
				   unsigned int num);
void devcon_page_scroll_up(struct devcon_page *page,
			   unsigned int num,
			   const struct devcon_attr *attr,
			   u64 age,
			   struct devcon_history *history);
void devcon_page_scroll_down(struct devcon_page *page,
			     unsigned int num,
			     const struct devcon_attr *attr,
			     u64 age,
			     struct devcon_history *history);
void devcon_page_insert_lines(struct devcon_page *page,
			      unsigned int pos_y,
			      unsigned int num,
			      const struct devcon_attr *attr,
			      u64 age);
void devcon_page_delete_lines(struct devcon_page *page,
			      unsigned int pos_y,
			      unsigned int num,
			      const struct devcon_attr *attr,
			      u64 age);

/*
 * Histories
 * Scroll-back buffers use devcon_history objects to store scroll-back lines. A
 * page is independent of the history used. All page operations that modify a
 * history take it as separate argument. You're free to pass NULL at all times
 * if no history should be used.
 * Lines are stored in a linked list as no complex operations are ever done on
 * history lines, besides pushing/poping. Note that history lines do not have a
 * guaranteed minimum length. Any kind of line might be stored there. Missing
 * cells should be cleared to the background color.
 */

struct devcon_history {
	struct list_head lines;
	unsigned int n_lines;
	unsigned int max_lines;
};

int devcon_history_new(struct devcon_history **out);
struct devcon_history *devcon_history_free(struct devcon_history *history);

void devcon_history_clear(struct devcon_history *history);
void devcon_history_trim(struct devcon_history *history, unsigned int max);
void devcon_history_push(struct devcon_history *history,
			 struct devcon_line *line);
struct devcon_line *devcon_history_pop(struct devcon_history *history,
				       unsigned int reserve_width,
				       const struct devcon_attr *attr,
				       u64 age);
unsigned int devcon_history_peek(struct devcon_history *history,
				 unsigned int max,
				 unsigned int reserve_width,
				 const struct devcon_attr *attr,
				 u64 age);

#endif /* __DEVCON_PAGE_H */
