/*
 * uni.h -- Unicode text layer for plain PM applications
 *
 * Strings are kept as UCS-2 (UniChar, 16 bit).  They are drawn with GPI
 * in code page 1200 using a Unicode-capable outline font.  Everything PM
 * cannot display natively (menus, title bar) goes through ULS conversion
 * into the process code page or through owner drawing.
 *
 * Findings from probe\probe.c on ArcaOS 5.1 (documented in README.md):
 *   - GpiCharStringAt / GpiQueryTextBox take the length of a CP 1200
 *     string in BYTES, not characters
 *   - "Droid Sans Combined", "Times New Roman MT 30" and the WT J fonts
 *     contain Kana, Kanji/Hanzi and Hangul glyphs
 */

#ifndef UNI_H
#define UNI_H

#define INCL_DOS
#define INCL_WIN
#define INCL_GPI
#include <os2.h>

/* ---- UTF-8 -> UCS-2 ---------------------------------------------- */

/* Decodes src (srclen bytes) into dst (room for dstmax chars incl. the
 * terminating 0).  Characters outside the BMP and malformed sequences
 * become U+FFFD; *invalid counts them.  Returns the number of chars. */
long utf8_to_ucs2(const unsigned char *src, long srclen,
                  USHORT *dst, long dstmax, long *invalid);

/* ---- ULS (UCONV.DLL), loaded at run time ------------------------- */

int  uls_init(void);          /* 1 = ULS available, 0 = ASCII fallback */
/* UCS-2 -> current process code page; unmappable characters become '?' */
int  uls_to_cp(const USHORT *s, int n, char *out, int outmax);
ULONG uls_codepage(void);

/* ---- fonts and drawing ------------------------------------------ */

#define UNI_LCID 7L           /* local character set id used for text */

/* Select face at pels (device pels) for code page 1200 into hps.
 * Returns 1 when the face exists (FONT_MATCH).  Always pair with
 * uni_end(). */
int  uni_begin(HPS hps, const char *face, int pels, ULONG *oldcp);
void uni_end(HPS hps, ULONG oldcp);

int  uni_width(HPS hps, const USHORT *s, int n);
int  uni_height(HPS hps);                       /* line height in pels */
int  uni_descender(HPS hps);
void uni_draw(HPS hps, long x, long y, const USHORT *s, int n);

/* Does this face draw a visible glyph for every character of s? */
int  uni_covers(const char *face, const USHORT *s, int n);

/* Frees the ULS object and the coverage-test bitmap. */
void uni_done(void);

/* Convert plain ASCII to a temporary UCS-2 buffer (max 255 chars). */
USHORT *uni_ascii(const char *s, int *n);

#endif
