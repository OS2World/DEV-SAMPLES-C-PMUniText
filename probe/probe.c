/*
 * probe.c -- what does GPI do with UCS-2 (code page 1200) on this system?
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

static int select_face(const char *face)
{
    FATTRS fat;
    SIZEF  sz;
    LONG   rc;

    memset(&fat, 0, sizeof(fat));
    fat.usRecordLength = sizeof(FATTRS);
    strncpy(fat.szFacename, face, FACESIZE - 1);
    fat.usCodePage = 1200;
    fat.fsFontUse = FATTR_FONTUSE_OUTLINE | FATTR_FONTUSE_TRANSFORMABLE;
    GpiDeleteSetId(hps, 1L);
    rc = GpiCreateLogFont(hps, NULL, 1L, &fat);
    if (rc != FONT_MATCH) return 0;
    GpiSetCharSet(hps, 1L);
    sz.cx = MAKEFIXED(PEL_SIZE, 0);
    sz.cy = MAKEFIXED(PEL_SIZE, 0);
    GpiSetCharBox(hps, &sz);
    GpiSetCp(hps, 1200L);
    return 1;
}

static void probe_face(const char *face)
{
    unsigned long hnone, h;
    long pnone, p;
    unsigned i;
    char line[200];

    if (!select_face(face)) {
        fprintf(flog, "  %-32s : no FONT_MATCH with cp 1200\n", face);
        return;
    }
    pnone = render(sNone, 2, &hnone);
    line[0] = 0;
    for (i = 0; i < NSCRIPTS; i++) {
        USHORT first[2];
        first[0] = scripts[i].text[0]; first[1] = 0;
        p = render(first, 2, &h);
        if (p > 0 && !(h == hnone && p == pnone)) {
            strcat(line, scripts[i].name);
            strcat(line, " ");
        }
    }
    fprintf(flog, "  %-32s : %s\n", face, line[0] ? line : "(Latin/none only)");
    GpiSetCharSet(hps, LCID_DEFAULT);
}

static void count_semantics(const char *face)
{
    POINTL pt[TXTBOX_COUNT];
    LONG w4, w8;
    static const USHORT aaaa[] = { 'A', 'A', 'A', 'A', 0 };

    if (!select_face(face)) return;
    GpiQueryTextBox(hps, 8L, (PCH)aaaa, TXTBOX_COUNT, pt); w8 = pt[TXTBOX_BOTTOMRIGHT].x - pt[TXTBOX_BOTTOMLEFT].x;
    GpiQueryTextBox(hps, 4L, (PCH)aaaa, TXTBOX_COUNT, pt); w4 = pt[TXTBOX_BOTTOMRIGHT].x - pt[TXTBOX_BOTTOMLEFT].x;
    fprintf(flog, "length semantics (%s, AAAA as UCS-2): width(count=8)=%ld width(count=4)=%ld\n", face, w8, w4);
    fprintf(flog, "  => the count is in %s\n", (w8 > w4 && w4 > 0) ? "ambiguous: 8 and 4 both measured, 8 is wider (4 chars drawn for 8 -> BYTES)" : "see numbers");
    GpiSetCharSet(hps, LCID_DEFAULT);
}

int main(void)
{
    LONG lCount = 0, i, nUni = 0;
    PFONTMETRICS pfm;
    char seen[400][FACESIZE];
    int nseen = 0;
    ULONG cp[4], cb = 0;

    flog = fopen("probe.log", "w");
    if (!flog) return 1;
    hab = WinInitialize(0);
    hmq = WinCreateMsgQueue(hab, 0);
    DosQueryCp(sizeof(cp), cp, &cb);
    fprintf(flog, "process codepage %lu\n", (unsigned long)cp[0]);
    if (!setup_memps()) {
        fprintf(flog, "memory PS setup failed (err %lx)\n", (unsigned long)WinGetLastError(hab));
        fclose(flog);
        return 1;
    }

    lCount = GpiQueryFonts(hps, QF_PUBLIC | QF_PRIVATE, NULL, &lCount, sizeof(FONTMETRICS), NULL);
    fprintf(flog, "fonts reported: %ld\n", lCount);
    pfm = (PFONTMETRICS)malloc(lCount * sizeof(FONTMETRICS));
    GpiQueryFonts(hps, QF_PUBLIC | QF_PRIVATE, NULL, &lCount, sizeof(FONTMETRICS), pfm);

    fprintf(flog, "\n== faces (fsType: 0x40=UNICODE 0x10=DBCS 0x08=MBCS; outline=fsDefn&1) ==\n");
    for (i = 0; i < lCount; i++) {
        int j, dup = 0;
        for (j = 0; j < nseen; j++) if (!strcmp(seen[j], pfm[i].szFacename)) { dup = 1; break; }
        if (dup) continue;
        if (nseen < 400) strcpy(seen[nseen++], pfm[i].szFacename);
        fprintf(flog, "%-32s fam=%-24s cp=%-5d reg=%-5d type=0x%04x defn=0x%04x\n",
                pfm[i].szFacename, pfm[i].szFamilyname, pfm[i].usCodePage, pfm[i].idRegistry,
                pfm[i].fsType, pfm[i].fsDefn);
        if (pfm[i].fsType & FM_TYPE_UNICODE) nUni++;
    }
    fprintf(flog, "unique faces %d, flagged UNICODE %ld\n", nseen, nUni);

    fprintf(flog, "\n== glyph coverage per outline face at %d pels, cp 1200 ==\n", PEL_SIZE);
    for (i = 0; i < nseen; i++) {
        int j;
        for (j = 0; j < lCount; j++)
            if (!strcmp(pfm[j].szFacename, seen[i])) break;
        if (j < lCount && (pfm[j].fsDefn & FM_DEFN_OUTLINE))
            probe_face(seen[i]);
    }
    fprintf(flog, "\n");
    count_semantics("Times New Roman MT 30");
    fprintf(flog, "done\n");
    fclose(flog);
    GpiSetBitmap(hps, NULLHANDLE);
    GpiDeleteBitmap(hbm);
    GpiDestroyPS(hps);
    DevCloseDC(hdc);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}
