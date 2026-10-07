/*
 * uni.c -- Unicode text layer for plain PM applications (see uni.h)
 */

#include "uni.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* UTF-8 -> UCS-2                                                      */
/* ------------------------------------------------------------------ */

long utf8_to_ucs2(const unsigned char *src, long srclen,
                  USHORT *dst, long dstmax, long *invalid)
{
    long i = 0, n = 0;

    if (invalid) *invalid = 0;
    if (dstmax < 1) return 0;
    while (i < srclen && n < dstmax - 1) {
        unsigned long cp;
        unsigned char c = src[i];
        int extra, k, bad = 0;

        if (c < 0x80)                { cp = c;        extra = 0; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else                         { cp = 0xFFFD;   extra = 0; bad = 1; }
        i++;
        for (k = 0; k < extra; k++) {
            if (i < srclen && (src[i] & 0xC0) == 0x80) {
                cp = (cp << 6) | (src[i] & 0x3F);
                i++;
            } else {
                bad = 1;
                break;
            }
        }
        if (bad || cp > 0xFFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            /* not representable in UCS-2 (GPI has no surrogate support) */
            cp = 0xFFFD;
            if (invalid) (*invalid)++;
        }
        dst[n++] = (USHORT)cp;
    }
    dst[n] = 0;
    return n;
}

/* ------------------------------------------------------------------ */
/* ULS, loaded at run time so no import library is needed              */
/* ------------------------------------------------------------------ */

typedef int (APIENTRY *PFN_CREATE)(USHORT *code_set, void **uobj);
typedef int (APIENTRY *PFN_FROMUCS)(void *uobj, USHORT **inbuf, size_t *inchars,
                                    void **outbuf, size_t *outbytes, size_t *subst);
typedef int (APIENTRY *PFN_FREE)(void *uobj);

static HMODULE     hUconv;
static PFN_CREATE  pfnCreate;
static PFN_FROMUCS pfnFromUcs;
static PFN_FREE    pfnFree;
static void       *uobj;
static ULONG       ulCp = 850;

int uls_init(void)
{
    char   fail[64];
    ULONG  cp[4], cb = 0;
    USHORT name[24];
    char   tmp[24];
    int    i;

    if (DosQueryCp(sizeof(cp), cp, &cb) == 0) ulCp = cp[0];
    if (DosLoadModule(fail, sizeof(fail), "UCONV", &hUconv) != 0) return 0;
    if (DosQueryProcAddr(hUconv, 0, "UniCreateUconvObject", (PFN *)&pfnCreate) ||
        DosQueryProcAddr(hUconv, 0, "UniUconvFromUcs", (PFN *)&pfnFromUcs) ||
        DosQueryProcAddr(hUconv, 0, "UniFreeUconvObject", (PFN *)&pfnFree)) {
        DosFreeModule(hUconv); hUconv = 0;
        return 0;
    }
    sprintf(tmp, "IBM-%lu", (unsigned long)ulCp);
    for (i = 0; tmp[i]; i++) name[i] = (USHORT)tmp[i];
    name[i] = 0;
    if (pfnCreate(name, &uobj) != 0) {
        DosFreeModule(hUconv); hUconv = 0; uobj = NULL;
        return 0;
    }
    return 1;
}

ULONG uls_codepage(void) { return ulCp; }

int uls_to_cp(const USHORT *s, int n, char *out, int outmax)
{
    int i, o = 0;

    for (i = 0; i < n && o < outmax - 1; i++) {
        if (s[i] < 0x80) {                 /* ASCII needs no table */
            out[o++] = (char)s[i];
        } else if (uobj) {
            USHORT  in[2];
            USHORT *pin = in;
            char    buf[8];
            void   *pout = buf;
            size_t  inchars = 1, outbytes = sizeof(buf), subst = 0;
            int     rc;

            in[0] = s[i]; in[1] = 0;
            rc = pfnFromUcs(uobj, &pin, &inchars, &pout, &outbytes, &subst);
            if (rc == 0 && subst == 0 && sizeof(buf) - outbytes > 0 &&
                o + (int)(sizeof(buf) - outbytes) < outmax) {
                int k, cnt = (int)(sizeof(buf) - outbytes);
                for (k = 0; k < cnt; k++) out[o++] = buf[k];
            } else {
                out[o++] = '?';
            }
        } else {
            out[o++] = '?';
        }
    }
    out[o] = 0;
    return o;
}

/* ------------------------------------------------------------------ */
/* fonts and drawing                                                   */
/* ------------------------------------------------------------------ */

int uni_begin(HPS hps, const char *face, int pels, ULONG *oldcp)
{
    FATTRS fat;
    SIZEF  sz;
    LONG   rc;

    *oldcp = GpiQueryCp(hps);
    memset(&fat, 0, sizeof(fat));
    fat.usRecordLength = sizeof(FATTRS);
    strncpy(fat.szFacename, face, FACESIZE - 1);
    fat.usCodePage = 1200;
    fat.fsFontUse  = FATTR_FONTUSE_OUTLINE | FATTR_FONTUSE_TRANSFORMABLE;
    GpiDeleteSetId(hps, UNI_LCID);
    rc = GpiCreateLogFont(hps, NULL, UNI_LCID, &fat);
    if (rc != FONT_MATCH) return 0;
    GpiSetCharSet(hps, UNI_LCID);
    sz.cx = MAKEFIXED(pels, 0);
    sz.cy = MAKEFIXED(pels, 0);
    GpiSetCharBox(hps, &sz);
    GpiSetCp(hps, 1200L);
    return 1;
}

void uni_end(HPS hps, ULONG oldcp)
{
    if (oldcp) GpiSetCp(hps, oldcp);
    GpiSetCharSet(hps, LCID_DEFAULT);
    GpiDeleteSetId(hps, UNI_LCID);
}

#define CHUNK 200            /* chars per GPI call, stays below 512 bytes */

int uni_width(HPS hps, const USHORT *s, int n)
{
    POINTL pt[TXTBOX_COUNT];
    int total = 0;

    while (n > 0) {
        int c = n > CHUNK ? CHUNK : n;
        if (GpiQueryTextBox(hps, c * 2L, (PCH)s, TXTBOX_COUNT, pt))
            total += (int)(pt[TXTBOX_CONCAT].x - pt[TXTBOX_BOTTOMLEFT].x);
        s += c; n -= c;
    }
    return total;
}

int uni_height(HPS hps)
{
    FONTMETRICS fm;
    SIZEF sz;
    int h = 0;

    if (GpiQueryFontMetrics(hps, sizeof(fm), &fm)) h = (int)fm.lMaxBaselineExt;
    if (GpiQueryCharBox(hps, &sz)) {
        int box = (int)(sz.cy >> 16);
        if (h < box) h = box + box / 4;      /* metrics not scaled: estimate */
    }
    return h;
}

int uni_descender(HPS hps)
{
    FONTMETRICS fm;
    if (GpiQueryFontMetrics(hps, sizeof(fm), &fm)) return (int)fm.lMaxDescender;
    return 0;
}

void uni_draw(HPS hps, long x, long y, const USHORT *s, int n)
{
    POINTL pt;

    pt.x = x; pt.y = y;
    while (n > 0) {
        int c = n > CHUNK ? CHUNK : n;
        GpiCharStringAt(hps, &pt, c * 2L, (PCH)s);
        s += c; n -= c;
        if (n > 0) pt.x = x + uni_width(hps, s - c, c);    /* next piece */
    }
}

USHORT *uni_ascii(const char *s, int *n)
{
    static USHORT buf[4][256];
    static int    k;
    USHORT *b = buf[k++ & 3];
    int i;

    for (i = 0; s[i] && i < 255; i++) b[i] = (unsigned char)s[i];
    b[i] = 0;
    if (n) *n = i;
    return b;
}

/* ------------------------------------------------------------------ */
/* glyph coverage test: render into a memory bitmap and compare with   */
/* what the face draws for the unassigned code U+0378                  */
/* ------------------------------------------------------------------ */

#define COV 56

static HAB     hab;
static HDC     hdcMem;
static HPS     hpsMem;
static HBITMAP hbmMem;

static int cov_setup(void)
{
    SIZEL sizl;
    LONG  fmt[2];
    BITMAPINFOHEADER2 bmih;

    if (hpsMem) return 1;
    hab = WinQueryAnchorBlock(HWND_DESKTOP);
    sizl.cx = COV; sizl.cy = COV;
    hdcMem = DevOpenDC(hab, OD_MEMORY, "*", 0L, NULL, NULLHANDLE);
    if (!hdcMem) return 0;
    hpsMem = GpiCreatePS(hab, hdcMem, &sizl, PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC);
    if (!hpsMem) return 0;
    if (!GpiQueryDeviceBitmapFormats(hpsMem, 2L, fmt)) { fmt[0] = 1; fmt[1] = 8; }
    memset(&bmih, 0, sizeof(bmih));
    bmih.cbFix = sizeof(BITMAPINFOHEADER2);
    bmih.cx = COV; bmih.cy = COV;
    bmih.cPlanes = (USHORT)fmt[0]; bmih.cBitCount = (USHORT)fmt[1];
    hbmMem = GpiCreateBitmap(hpsMem, &bmih, 0L, NULL, NULL);
    if (!hbmMem || GpiSetBitmap(hpsMem, hbmMem) == HBM_ERROR) return 0;
    return 1;
}

static long cov_render(USHORT ch, unsigned long *hash)
{
    POINTL pt, p;
    RECTL  rcl;
    long   x, y, cnt = 0, bg;
    unsigned long h = 5381;

    rcl.xLeft = 0; rcl.yBottom = 0; rcl.xRight = COV; rcl.yTop = COV;
    WinFillRect(hpsMem, &rcl, CLR_WHITE);
    p.x = 0; p.y = 0;
    bg = GpiQueryPel(hpsMem, &p);
    GpiSetColor(hpsMem, CLR_BLACK);
    pt.x = 4; pt.y = 12;
    GpiCharStringAt(hpsMem, &pt, 2L, (PCH)&ch);
    for (y = 0; y < COV; y++)
        for (x = 0; x < COV; x++) {
            p.x = x; p.y = y;
            if (GpiQueryPel(hpsMem, &p) != bg) {
                cnt++;
                h = h * 33 + (unsigned long)(x * 131 + y);
            }
        }
    *hash = h;
    return cnt;
}

int uni_covers(const char *face, const USHORT *s, int n)
{
    ULONG oldcp;
    unsigned long hNone, h;
    long pNone, p;
    int i, checked = 0, ok = 1;

    if (!cov_setup()) return 1;                  /* cannot test: assume yes */
    if (!uni_begin(hpsMem, face, 28, &oldcp)) { uni_end(hpsMem, oldcp); return 0; }
    pNone = cov_render(0x0378, &hNone);
    for (i = 0; i < n && checked < 6; i++) {
        if (s[i] < 0x80) continue;               /* Latin is always there */
        checked++;
        p = cov_render(s[i], &h);
        if (p == 0 || (p == pNone && h == hNone)) { ok = 0; break; }
    }
    uni_end(hpsMem, oldcp);
    return ok;
}

void uni_done(void)
{
    if (hpsMem) {
        GpiSetBitmap(hpsMem, NULLHANDLE);
        if (hbmMem) GpiDeleteBitmap(hbmMem);
        GpiDestroyPS(hpsMem);
        DevCloseDC(hdcMem);
        hpsMem = NULLHANDLE; hbmMem = NULLHANDLE; hdcMem = NULLHANDLE;
    }
    if (uobj)  { pfnFree(uobj); uobj = NULL; }
    if (hUconv) { DosFreeModule(hUconv); hUconv = 0; }
}
