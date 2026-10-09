/*
 * probe1208.c -- does GPI accept UTF-8 (code page 1208) text directly?
 *
 * Writes probe.log next to the exe:
 *   1. every installed face with its FONTMETRICS type flags
 *   2. for each outline face: does it draw glyphs for Latin, Greek,
 *      Cyrillic, Japanese, Chinese, Korean, Arabic? (rendered into a
 *      memory bitmap; a script counts as "yes" when the glyph differs
 *      from what the same face draws for the unassigned code U+0378)
 *   3. whether the string length for CP 1200 is counted in bytes or chars
 *
 * Plain C, no resources. Build: build-probe.cmd
 */

#define INCL_DOS
#define INCL_WIN
#define INCL_GPI
#define INCL_DEV
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BMP_CX 160
#define BMP_CY 48
#define PEL_SIZE 28

static FILE *flog;

typedef struct {
    const char     *name;
    const USHORT   *text;     /* UCS-2, zero terminated */
} SCRIPT;

static const USHORT sLatin[]  = { 0x0048, 0x0065, 0x006C, 0x006C, 0x006F, 0 };
static const USHORT sGreek[]  = { 0x0395, 0x03BB, 0x03BB, 0x03B7, 0x03BD, 0 };
static const USHORT sCyril[]  = { 0x041F, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442, 0 };
static const USHORT sJapan[]  = { 0x65E5, 0x672C, 0x8A9E, 0 };
static const USHORT sChina[]  = { 0x4E2D, 0x6587, 0 };
static const USHORT sKorea[]  = { 0xD55C, 0xAD6D, 0xC5B4, 0 };
static const USHORT sArab[]   = { 0x0645, 0x0631, 0x062D, 0x0628, 0x0627, 0 };
static const USHORT sKana[]   = { 0x3042, 0x30A2, 0 };
static const USHORT sNone[]   = { 0x0378, 0 };

static SCRIPT scripts[] = {
    { "Latin", sLatin }, { "Greek", sGreek }, { "Cyrillic", sCyril },
    { "Kana",  sKana  }, { "Kanji-JP", sJapan }, { "Hanzi-CN", sChina },
    { "Hangul", sKorea }, { "Arabic", sArab }
};
#define NSCRIPTS (sizeof(scripts) / sizeof(scripts[0]))

static HAB  hab;
static HMQ  hmq;
static HDC  hdc;
static HPS  hps;
static HBITMAP hbm;

static int ucslen(const USHORT *s) { int n = 0; while (s[n]) n++; return n; }

/* make a memory PS with a colour bitmap in the screen format */
static int setup_memps(void)
{
    SIZEL sizl;
    LONG  fmt[2];
    BITMAPINFOHEADER2 bmih;

    sizl.cx = BMP_CX; sizl.cy = BMP_CY;
    hdc = DevOpenDC(hab, OD_MEMORY, "*", 0L, NULL, NULLHANDLE);
    if (!hdc) return 0;
    hps = GpiCreatePS(hab, hdc, &sizl, PU_PELS | GPIF_DEFAULT | GPIT_MICRO | GPIA_ASSOC);
    if (!hps) return 0;
    if (!GpiQueryDeviceBitmapFormats(hps, 2L, fmt)) { fmt[0] = 1; fmt[1] = 8; }
    memset(&bmih, 0, sizeof(bmih));
    bmih.cbFix = sizeof(BITMAPINFOHEADER2);
    bmih.cx = BMP_CX; bmih.cy = BMP_CY;
    bmih.cPlanes = (USHORT)fmt[0]; bmih.cBitCount = (USHORT)fmt[1];
    hbm = GpiCreateBitmap(hps, &bmih, 0L, NULL, NULL);
    if (!hbm) return 0;
    if (GpiSetBitmap(hps, hbm) == HBM_ERROR) return 0;
    return 1;
}

/* draw text (CP 1200) and return number of black pels; *hash gets a checksum */
static long render(const USHORT *text, int nbytes, unsigned long *hash)
{
    POINTL ptl;
    RECTL  rcl;
    long   x, y, cnt = 0, bg;
    unsigned long h = 5381;

    rcl.xLeft = 0; rcl.yBottom = 0; rcl.xRight = BMP_CX; rcl.yTop = BMP_CY;
    WinFillRect(hps, &rcl, CLR_WHITE);
    GpiSetColor(hps, CLR_BLACK);
    { POINTL p0; p0.x = 0; p0.y = 0; bg = GpiQueryPel(hps, &p0); }
    ptl.x = 4; ptl.y = 12;
    GpiCharStringAt(hps, &ptl, nbytes, (PCH)text);
    for (y = 0; y < BMP_CY; y++)
        for (x = 0; x < BMP_CX; x++) {
            POINTL p;
            LONG c;
            p.x = x; p.y = y;
            c = GpiQueryPel(hps, &p);
            if (c != bg) {
                cnt++;
                h = h * 33 + (unsigned long)(x * 131 + y);
            }
        }
    *hash = h;
    return cnt;
}


static const char *rcname(LONG rc)
{
    if (rc == FONT_MATCH) return "FONT_MATCH";
    if (rc == GPI_ERROR)  return "GPI_ERROR";
    return "other";
}

/* font created with cpFont, GpiSetCp(cpSet); cpSet<0: GpiSetCp not called */
static int select_cp(const char *face, int cpFont, int cpSet, const char *tag)
{
    FATTRS fat;
    SIZEF  sz;
    LONG   rc, rcCp = -99;

    memset(&fat, 0, sizeof(fat));
    fat.usRecordLength = sizeof(FATTRS);
    strncpy(fat.szFacename, face, FACESIZE - 1);
    fat.usCodePage = (USHORT)cpFont;
    fat.fsFontUse = FATTR_FONTUSE_OUTLINE | FATTR_FONTUSE_TRANSFORMABLE;
    GpiDeleteSetId(hps, 1L);
    rc = GpiCreateLogFont(hps, NULL, 1L, &fat);
    if (rc != FONT_MATCH) {
        fprintf(flog, "  [%s] GpiCreateLogFont(cp %d) = %s (%ld), err 0x%lx\n", tag, cpFont, rcname(rc), rc,
                (unsigned long)WinGetLastError(hab));
        return 0;
    }
    GpiSetCharSet(hps, 1L);
    sz.cx = MAKEFIXED(PEL_SIZE, 0);
    sz.cy = MAKEFIXED(PEL_SIZE, 0);
    GpiSetCharBox(hps, &sz);
    if (cpSet >= 0) rcCp = GpiSetCp(hps, (ULONG)cpSet);
    fprintf(flog, "  [%s] GpiCreateLogFont(cp %d)=FONT_MATCH, GpiSetCp(%d)=%ld, GpiQueryCp=%lu\n", tag, cpFont, cpSet,
            rcCp, (unsigned long)GpiQueryCp(hps));
    return 1;
}

typedef struct { const char *name; const USHORT *ucs; const char *utf8; } SAMPLE;
static const char u8Latin[] = "Hello";
static const char u8Greek[] = "\xCE\x95\xCE\xBB\xCE\xBB\xCE\xB7\xCE\xBD";
static const char u8Japan[] = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";
static const char u8Cyril[] = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82";
static const char u8Korea[] = "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4";
static SAMPLE samples[] = {
    { "Latin",    sLatin, u8Latin }, { "Greek", sGreek, u8Greek }, { "Cyrillic", sCyril, u8Cyril },
    { "Japanese", sJapan, u8Japan }, { "Korean", sKorea, u8Korea }
};

static void trial(const char *face, int cpFont, int cpSet, const char *tag, int useUtf8)
{
    unsigned long href, h;
    long pref, p;
    unsigned i;

    fprintf(flog, "\n-- %s --\n", tag);
    /* reference: the same text as UCS-2 in code page 1200 */
    if (!select_cp(face, 1200, 1200, "reference 1200")) return;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        pref = render(samples[i].ucs, ucslen(samples[i].ucs) * 2, &href);
        fprintf(flog, "    ref %-9s pels=%ld hash=%lu\n", samples[i].name, pref, href);
    }
    GpiSetCharSet(hps, LCID_DEFAULT);
    if (!select_cp(face, cpFont, cpSet, tag)) return;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        unsigned long hr;
        long pr;
        GpiSetCharSet(hps, LCID_DEFAULT);
        select_cp(face, 1200, 1200, "ref");   /* (log noise ok) */
        pr = render(samples[i].ucs, ucslen(samples[i].ucs) * 2, &hr);
        GpiSetCharSet(hps, LCID_DEFAULT);
        select_cp(face, cpFont, cpSet, tag);
        p = render((const USHORT *)samples[i].utf8, (int)strlen(samples[i].utf8), &h);
        fprintf(flog, "    %-9s utf8 bytes=%d pels=%ld (ref %ld) %s\n", samples[i].name, (int)strlen(samples[i].utf8), p, pr,
                (p == pr && h == hr) ? "IDENTICAL to the UCS-2 rendering" : "DIFFERENT");
    }
    GpiSetCharSet(hps, LCID_DEFAULT);
}


static void nonbmp(const char *face)
{
    static const struct { const char *name; const char *u8; } cs[] = {
        { "U+FFFD replacement", "\xEF\xBF\xBD" },
        { "U+0378 unassigned",  "\xCD\xB8" },
        { "U+20000 CJK Ext-B",  "\xF0\xA0\x80\x80" },
        { "U+1F600 emoji",      "\xF0\x9F\x98\x80" },
        { "U+4E2D (BMP kanji)", "\xE4\xB8\xAD" }
    };
    unsigned i;
    unsigned long h;
    long p;

    fprintf(flog, "\n-- non-BMP in code page 1208, face %s --\n", face);
    if (!select_cp(face, 1208, 1208, "1208")) return;
    for (i = 0; i < sizeof(cs) / sizeof(cs[0]); i++) {
        p = render((const USHORT *)cs[i].u8, (int)strlen(cs[i].u8), &h);
        fprintf(flog, "    %-20s bytes=%d pels=%ld hash=%lu\n", cs[i].name, (int)strlen(cs[i].u8), p, h);
    }
    GpiSetCharSet(hps, LCID_DEFAULT);
}

int main(void)
{
    ULONG cp[4], cb = 0;
    const char *face = "Times New Roman MT 30";

    flog = fopen("probe1208.log", "w");
    if (!flog) return 1;
    hab = WinInitialize(0);
    hmq = WinCreateMsgQueue(hab, 0);
    DosQueryCp(sizeof(cp), cp, &cb);
    fprintf(flog, "process codepage %lu, face %s\n", (unsigned long)cp[0], face);
    if (!setup_memps()) {
        fprintf(flog, "memory PS setup failed\n");
        fclose(flog);
        return 1;
    }
    trial(face, 1208, 1208, "font cp 1208 + GpiSetCp 1208", 1);
    trial(face, 1200, 1208, "font cp 1200, GpiSetCp 1208", 1);
    trial(face, 0,    1208, "font cp 0 (default), GpiSetCp 1208", 1);
    trial(face, 1208, -1,   "font cp 1208, no GpiSetCp", 1);
    nonbmp(face);
    nonbmp("Droid Sans Combined");
    fprintf(flog, "\ndone\n");
    fclose(flog);
    GpiSetBitmap(hps, NULLHANDLE);
    GpiDeleteBitmap(hbm);
    GpiDestroyPS(hps);
    DevCloseDC(hdc);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}
