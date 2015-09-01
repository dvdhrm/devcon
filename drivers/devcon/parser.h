/*
 * Copyright (C) 2015 David Herrmann <dh.herrmann@gmail.com>
 *
 * devcon is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 */

#ifndef __DEVCON_PARSER_H
#define __DEVCON_PARSER_H

#include <linux/kernel.h>

struct devcon_parser;
struct devcon_seq;
struct devcon_utf8;

/*
 * Charsets
 * The DEC-compatible terminals require non-standard charsets for g0/g1/g2/g3
 * registers. We only provide the basic sets for compatibility. New
 * applications better use the full UTF-8 range for that.
 */

typedef uint32_t devcon_charset[96];

extern devcon_charset devcon_unicode_lower;
extern devcon_charset devcon_unicode_upper;
extern devcon_charset devcon_dec_supplemental_graphics;
extern devcon_charset devcon_dec_special_graphics;

/*
 * UTF-8
 * All stream data must be encoded as UTF-8. As we need to do glyph-rendering,
 * we require a UTF-8 parser so we can map the characters to UCS codepoints.
 */

struct devcon_utf8 {
	u32 chars[5];
	u32 ucs4;

	unsigned int i_bytes : 3;
	unsigned int n_bytes : 3;
	unsigned int valid : 1;
};

size_t devcon_utf8_decode(struct devcon_utf8 *p, const u32 **out_buf, char c);
size_t devcon_utf8_encode(char *out_utf8, u32 g);

/*
 * Parsers
 * The devcon_parser object parses control-sequences for both host and terminal
 * side. Based on this parser, there is a set of command-parsers that take a
 * devcon_seq sequence and returns the command it represents. This is different
 * for host and terminal side, and so far we only provide the terminal side, as
 * host side is not used by anyone.
 */

#define DEVCON_PARSER_ARG_MAX (16)

enum {
	DEVCON_SEQ_NONE,	/* placeholder, no sequence parsed */

	DEVCON_SEQ_IGNORE,	/* no-op character */
	DEVCON_SEQ_GRAPHIC,	/* graphic character */
	DEVCON_SEQ_CONTROL,	/* control character */
	DEVCON_SEQ_ESCAPE,	/* escape sequence */
	DEVCON_SEQ_CSI,		/* control sequence function */
	DEVCON_SEQ_DCS,		/* device control string */
	DEVCON_SEQ_OSC,		/* operating system control */

	DEVCON_SEQ_N,
};

enum {
	/* these must be kept compatible to (1U << (ch - 0x20)) */

	DEVCON_SEQ_FLAG_SPACE		= (1U <<  0),	/* char:   */
	DEVCON_SEQ_FLAG_BANG		= (1U <<  1),	/* char: ! */
	DEVCON_SEQ_FLAG_DQUOTE		= (1U <<  2),	/* char: " */
	DEVCON_SEQ_FLAG_HASH		= (1U <<  3),	/* char: # */
	DEVCON_SEQ_FLAG_CASH		= (1U <<  4),	/* char: $ */
	DEVCON_SEQ_FLAG_PERCENT		= (1U <<  5),	/* char: % */
	DEVCON_SEQ_FLAG_AND		= (1U <<  6),	/* char: & */
	DEVCON_SEQ_FLAG_SQUOTE		= (1U <<  7),	/* char: ' */
	DEVCON_SEQ_FLAG_POPEN		= (1U <<  8),	/* char: ( */
	DEVCON_SEQ_FLAG_PCLOSE		= (1U <<  9),	/* char: ) */
	DEVCON_SEQ_FLAG_MULT		= (1U << 10),	/* char: * */
	DEVCON_SEQ_FLAG_PLUS		= (1U << 11),	/* char: + */
	DEVCON_SEQ_FLAG_COMMA		= (1U << 12),	/* char: , */
	DEVCON_SEQ_FLAG_MINUS		= (1U << 13),	/* char: - */
	DEVCON_SEQ_FLAG_DOT		= (1U << 14),	/* char: . */
	DEVCON_SEQ_FLAG_SLASH		= (1U << 15),	/* char: / */

	/* 16-35 is reserved for numbers; unused */

	/* COLON is reserved		= (1U << 26),	   char: : */
	/* SEMICOLON is reserved	= (1U << 27),	   char: ; */
	DEVCON_SEQ_FLAG_LT		= (1U << 28),	/* char: < */
	DEVCON_SEQ_FLAG_EQUAL		= (1U << 29),	/* char: = */
	DEVCON_SEQ_FLAG_GT		= (1U << 30),	/* char: > */
	DEVCON_SEQ_FLAG_WHAT		= (1U << 31),	/* char: ? */
};

enum {
	DEVCON_CMD_NONE,		/* placeholder */
	DEVCON_CMD_GRAPHIC,		/* graphics character */

	DEVCON_CMD_BEL,			/* bell */
	DEVCON_CMD_BS,			/* backspace */
	DEVCON_CMD_CBT,			/* cursor-backward-tabulation */
	DEVCON_CMD_CHA,			/* cursor-horizontal-absolute */
	DEVCON_CMD_CHT,			/* cursor-horizontal-forward-tabulation */
	DEVCON_CMD_CNL,			/* cursor-next-line */
	DEVCON_CMD_CPL,			/* cursor-previous-line */
	DEVCON_CMD_CR,			/* carriage-return */
	DEVCON_CMD_CUB,			/* cursor-backward */
	DEVCON_CMD_CUD,			/* cursor-down */
	DEVCON_CMD_CUF,			/* cursor-forward */
	DEVCON_CMD_CUP,			/* cursor-position */
	DEVCON_CMD_CUU,			/* cursor-up */
	DEVCON_CMD_DA1,			/* primary-device-attributes */
	DEVCON_CMD_DA2,			/* secondary-device-attributes */
	DEVCON_CMD_DA3,			/* tertiary-device-attributes */
	DEVCON_CMD_DC1,			/* device-control-1 or XON */
	DEVCON_CMD_DC3,			/* device-control-3 or XOFF */
	DEVCON_CMD_DCH,			/* delete-character */
	DEVCON_CMD_DECALN,		/* screen-alignment-pattern */
	DEVCON_CMD_DECANM,		/* ansi-mode */
	DEVCON_CMD_DECBI,		/* back-index */
	DEVCON_CMD_DECCARA,		/* change-attributes-in-rectangular-area */
	DEVCON_CMD_DECCRA,		/* copy-rectangular-area */
	DEVCON_CMD_DECDC,		/* delete-column */
	DEVCON_CMD_DECDHL_BH,		/* double-width-double-height-line: bottom half */
	DEVCON_CMD_DECDHL_TH,		/* double-width-double-height-line: top half */
	DEVCON_CMD_DECDWL,		/* double-width-single-height-line */
	DEVCON_CMD_DECEFR,		/* enable-filter-rectangle */
	DEVCON_CMD_DECELF,		/* enable-local-functions */
	DEVCON_CMD_DECELR,		/* enable-locator-reporting */
	DEVCON_CMD_DECERA,		/* erase-rectangular-area */
	DEVCON_CMD_DECFI,		/* forward-index */
	DEVCON_CMD_DECFRA,		/* fill-rectangular-area */
	DEVCON_CMD_DECIC,		/* insert-column */
	DEVCON_CMD_DECID,		/* return-terminal-id */
	DEVCON_CMD_DECINVM,		/* invoke-macro */
	DEVCON_CMD_DECKBD,		/* keyboard-language-selection */
	DEVCON_CMD_DECKPAM,		/* keypad-application-mode */
	DEVCON_CMD_DECKPNM,		/* keypad-numeric-mode */
	DEVCON_CMD_DECLFKC,		/* local-function-key-control */
	DEVCON_CMD_DECLL,		/* load-leds */
	DEVCON_CMD_DECLTOD,		/* load-time-of-day */
	DEVCON_CMD_DECPCTERM,		/* pcterm-mode */
	DEVCON_CMD_DECPKA,		/* program-key-action */
	DEVCON_CMD_DECPKFMR,		/* program-key-free-memory-report */
	DEVCON_CMD_DECRARA,		/* reverse-attributes-in-rectangular-area */
	DEVCON_CMD_DECRC,		/* restore-cursor */
	DEVCON_CMD_DECREQTPARM,		/* request-terminal-parameters */
	DEVCON_CMD_DECRPKT,		/* report-key-type */
	DEVCON_CMD_DECRQCRA,		/* request-checksum-of-rectangular-area */
	DEVCON_CMD_DECRQDE,		/* request-display-extent */
	DEVCON_CMD_DECRQKT,		/* request-key-type */
	DEVCON_CMD_DECRQLP,		/* request-locator-position */
	DEVCON_CMD_DECRQM_ANSI,		/* request-mode-ansi */
	DEVCON_CMD_DECRQM_DEC,		/* request-mode-dec */
	DEVCON_CMD_DECRQPKFM,		/* request-program-key-free-memory */
	DEVCON_CMD_DECRQPSR,		/* request-presentation-state-report */
	DEVCON_CMD_DECRQTSR,		/* request-terminal-state-report */
	DEVCON_CMD_DECRQUPSS,		/* request-user-preferred-supplemental-set */
	DEVCON_CMD_DECSACE,		/* select-attribute-change-extent */
	DEVCON_CMD_DECSASD,		/* select-active-status-display */
	DEVCON_CMD_DECSC,		/* save-cursor */
	DEVCON_CMD_DECSCA,		/* select-character-protection-attribute */
	DEVCON_CMD_DECSCL,		/* select-conformance-level */
	DEVCON_CMD_DECSCP,		/* select-communication-port */
	DEVCON_CMD_DECSCPP,		/* select-columns-per-page */
	DEVCON_CMD_DECSCS,		/* select-communication-speed */
	DEVCON_CMD_DECSCUSR,		/* set-cursor-style */
	DEVCON_CMD_DECSDDT,		/* select-disconnect-delay-time */
	DEVCON_CMD_DECSDPT,		/* select-digital-printed-data-type */
	DEVCON_CMD_DECSED,		/* selective-erase-in-display */
	DEVCON_CMD_DECSEL,		/* selective-erase-in-line */
	DEVCON_CMD_DECSERA,		/* selective-erase-rectangular-area */
	DEVCON_CMD_DECSFC,		/* select-flow-control */
	DEVCON_CMD_DECSKCV,		/* set-key-click-volume */
	DEVCON_CMD_DECSLCK,		/* set-lock-key-style */
	DEVCON_CMD_DECSLE,		/* select-locator-events */
	DEVCON_CMD_DECSLPP,		/* set-lines-per-page */
	DEVCON_CMD_DECSLRM_OR_SC,	/* set-left-and-right-margins or save-cursor */
	DEVCON_CMD_DECSMBV,		/* set-margin-bell-volume */
	DEVCON_CMD_DECSMKR,		/* select-modifier-key-reporting */
	DEVCON_CMD_DECSNLS,		/* set-lines-per-screen */
	DEVCON_CMD_DECSPP,		/* set-port-parameter */
	DEVCON_CMD_DECSPPCS,		/* select-pro-printer-character-set */
	DEVCON_CMD_DECSPRTT,		/* select-printer-type */
	DEVCON_CMD_DECSR,		/* secure-reset */
	DEVCON_CMD_DECSRFR,		/* select-refresh-rate */
	DEVCON_CMD_DECSSCLS,		/* set-scroll-speed */
	DEVCON_CMD_DECSSDT,		/* select-status-display-line-type */
	DEVCON_CMD_DECSSL,		/* select-setup-language */
	DEVCON_CMD_DECST8C,		/* set-tab-at-every-8-columns */
	DEVCON_CMD_DECSTBM,		/* set-top-and-bottom-margins */
	DEVCON_CMD_DECSTR,		/* soft-terminal-reset */
	DEVCON_CMD_DECSTRL,		/* set-transmit-rate-limit */
	DEVCON_CMD_DECSWBV,		/* set-warning-bell-volume */
	DEVCON_CMD_DECSWL,		/* single-width-single-height-line */
	DEVCON_CMD_DECTID,		/* select-terminal-id */
	DEVCON_CMD_DECTME,		/* terminal-mode-emulation */
	DEVCON_CMD_DECTST,		/* invoke-confidence-test */
	DEVCON_CMD_DL,			/* delete-line */
	DEVCON_CMD_DSR_ANSI,		/* device-status-report-ansi */
	DEVCON_CMD_DSR_DEC,		/* device-status-report-dec */
	DEVCON_CMD_ECH,			/* erase-character */
	DEVCON_CMD_ED,			/* erase-in-display */
	DEVCON_CMD_EL,			/* erase-in-line */
	DEVCON_CMD_ENQ,			/* enquiry */
	DEVCON_CMD_EPA,			/* end-of-guarded-area */
	DEVCON_CMD_FF,			/* form-feed */
	DEVCON_CMD_HPA,			/* horizontal-position-absolute */
	DEVCON_CMD_HPR,			/* horizontal-position-relative */
	DEVCON_CMD_HT,			/* horizontal-tab */
	DEVCON_CMD_HTS,			/* horizontal-tab-set */
	DEVCON_CMD_HVP,			/* horizontal-and-vertical-position */
	DEVCON_CMD_ICH,			/* insert-character */
	DEVCON_CMD_IL,			/* insert-line */
	DEVCON_CMD_IND,			/* index */
	DEVCON_CMD_LF,			/* line-feed */
	DEVCON_CMD_LS1R,		/* locking-shift-1-right */
	DEVCON_CMD_LS2,			/* locking-shift-2 */
	DEVCON_CMD_LS2R,		/* locking-shift-2-right */
	DEVCON_CMD_LS3,			/* locking-shift-3 */
	DEVCON_CMD_LS3R,		/* locking-shift-3-right */
	DEVCON_CMD_MC_ANSI,		/* media-copy-ansi */
	DEVCON_CMD_MC_DEC,		/* media-copy-dec */
	DEVCON_CMD_NEL,			/* next-line */
	DEVCON_CMD_NP,			/* next-page */
	DEVCON_CMD_NULL,		/* null */
	DEVCON_CMD_PP,			/* preceding-page */
	DEVCON_CMD_PPA,			/* page-position-absolute */
	DEVCON_CMD_PPB,			/* page-position-backward */
	DEVCON_CMD_PPR,			/* page-position-relative */
	DEVCON_CMD_RC,			/* restore-cursor */
	DEVCON_CMD_REP,			/* repeat */
	DEVCON_CMD_RI,			/* reverse-index */
	DEVCON_CMD_RIS,			/* reset-to-initial-state */
	DEVCON_CMD_RM_ANSI,		/* reset-mode-ansi */
	DEVCON_CMD_RM_DEC,		/* reset-mode-dec */
	DEVCON_CMD_S7C1T,		/* set-7bit-c1-terminal */
	DEVCON_CMD_S8C1T,		/* set-8bit-c1-terminal */
	DEVCON_CMD_SCS,			/* select-character-set */
	DEVCON_CMD_SD,			/* scroll-down */
	DEVCON_CMD_SGR,			/* select-graphics-rendition */
	DEVCON_CMD_SI,			/* shift-in */
	DEVCON_CMD_SM_ANSI,		/* set-mode-ansi */
	DEVCON_CMD_SM_DEC,		/* set-mode-dec */
	DEVCON_CMD_SO,			/* shift-out */
	DEVCON_CMD_SPA,			/* start-of-protected-area */
	DEVCON_CMD_SS2,			/* single-shift-2 */
	DEVCON_CMD_SS3,			/* single-shift-3 */
	DEVCON_CMD_ST,			/* string-terminator */
	DEVCON_CMD_SU,			/* scroll-up */
	DEVCON_CMD_SUB,			/* substitute */
	DEVCON_CMD_TBC,			/* tab-clear */
	DEVCON_CMD_VPA,			/* vertical-line-position-absolute */
	DEVCON_CMD_VPR,			/* vertical-line-position-relative */
	DEVCON_CMD_VT,			/* vertical-tab */
	DEVCON_CMD_XTERM_CLLHP,		/* xterm-cursor-lower-left-hp-bugfix */
	DEVCON_CMD_XTERM_IHMT,		/* xterm-initiate-highlight-mouse-tracking */
	DEVCON_CMD_XTERM_MLHP,		/* xterm-memory-lock-hp-bugfix */
	DEVCON_CMD_XTERM_MUHP,		/* xterm-memory-unlock-hp-bugfix */
	DEVCON_CMD_XTERM_RPM,		/* xterm-restore-private-mode */
	DEVCON_CMD_XTERM_RRV,		/* xterm-reset-resource-value */
	DEVCON_CMD_XTERM_RTM,		/* xterm-reset-title-mode */
	DEVCON_CMD_XTERM_SACL1,		/* xterm-set-ansi-conformance-level-1 */
	DEVCON_CMD_XTERM_SACL2,		/* xterm-set-ansi-conformance-level-2 */
	DEVCON_CMD_XTERM_SACL3,		/* xterm-set-ansi-conformance-level-3 */
	DEVCON_CMD_XTERM_SDCS,		/* xterm-set-default-character-set */
	DEVCON_CMD_XTERM_SGFX,		/* xterm-sixel-graphics */
	DEVCON_CMD_XTERM_SPM,		/* xterm-set-private-mode */
	DEVCON_CMD_XTERM_SRV,		/* xterm-set-resource-value */
	DEVCON_CMD_XTERM_STM,		/* xterm-set-title-mode */
	DEVCON_CMD_XTERM_SUCS,		/* xterm-set-utf8-character-set */
	DEVCON_CMD_XTERM_WM,		/* xterm-window-management */

	DEVCON_CMD_N,
};

enum {
	/*
	 * Charsets: DEC marks charsets according to "Digital Equ. Corp.".
	 *           NRCS marks charsets according to the "National Replacement
	 *           Character Sets". ISO marks charsets according to ISO-8859.
	 * The USERDEF charset is special and can be modified by the host.
	 */

	DEVCON_CHARSET_NONE,

	/* 96-compat charsets */
	DEVCON_CHARSET_ISO_LATIN1_SUPPLEMENTAL,
	DEVCON_CHARSET_BRITISH_NRCS = DEVCON_CHARSET_ISO_LATIN1_SUPPLEMENTAL,
	DEVCON_CHARSET_ISO_LATIN2_SUPPLEMENTAL,
	DEVCON_CHARSET_AMERICAN_NRCS = DEVCON_CHARSET_ISO_LATIN2_SUPPLEMENTAL,
	DEVCON_CHARSET_ISO_LATIN5_SUPPLEMENTAL,
	DEVCON_CHARSET_ISO_GREEK_SUPPLEMENTAL,
	DEVCON_CHARSET_ISO_HEBREW_SUPPLEMENTAL,
	DEVCON_CHARSET_ISO_LATIN_CYRILLIC,

	DEVCON_CHARSET_96_N,

	/* 94-compat charsets */
	DEVCON_CHARSET_DEC_SPECIAL_GRAPHIC = DEVCON_CHARSET_96_N,
	DEVCON_CHARSET_DEC_SUPPLEMENTAL,
	DEVCON_CHARSET_DEC_TECHNICAL,
	DEVCON_CHARSET_CYRILLIC_DEC,
	DEVCON_CHARSET_DUTCH_NRCS,
	DEVCON_CHARSET_FINNISH_NRCS,
	DEVCON_CHARSET_FRENCH_NRCS,
	DEVCON_CHARSET_FRENCH_CANADIAN_NRCS,
	DEVCON_CHARSET_GERMAN_NRCS,
	DEVCON_CHARSET_GREEK_DEC,
	DEVCON_CHARSET_GREEK_NRCS,
	DEVCON_CHARSET_HEBREW_DEC,
	DEVCON_CHARSET_HEBREW_NRCS,
	DEVCON_CHARSET_ITALIAN_NRCS,
	DEVCON_CHARSET_NORWEGIAN_DANISH_NRCS,
	DEVCON_CHARSET_PORTUGUESE_NRCS,
	DEVCON_CHARSET_RUSSIAN_NRCS,
	DEVCON_CHARSET_SCS_NRCS,
	DEVCON_CHARSET_SPANISH_NRCS,
	DEVCON_CHARSET_SWEDISH_NRCS,
	DEVCON_CHARSET_SWISS_NRCS,
	DEVCON_CHARSET_TURKISH_DEC,
	DEVCON_CHARSET_TURKISH_NRCS,

	DEVCON_CHARSET_94_N,

	/* special charsets */
	DEVCON_CHARSET_USERPREF_SUPPLEMENTAL = DEVCON_CHARSET_94_N,

	DEVCON_CHARSET_N,
};

struct devcon_seq {
	unsigned int type;
	unsigned int command;
	u32 terminator;
	unsigned int intermediates;
	unsigned int charset;
	unsigned int n_args;
	int args[DEVCON_PARSER_ARG_MAX];
	unsigned int n_st;
	char *st;
};

int devcon_parser_new(struct devcon_parser **out);
struct devcon_parser *devcon_parser_free(struct devcon_parser *parser);
int devcon_parser_feed(struct devcon_parser *parser,
		       const struct devcon_seq **seq_out,
		       u32 raw);

#endif /* __DEVCON_PARSER_H */
