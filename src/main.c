/*
 * main.c -- PMUniText: Unicode (UTF-8 language files) in a plain PM program
 *
 * - Language files lang\*.txt are UTF-8; they are decoded to UCS-2.
 * - Client area text is drawn with GPI in code page 1200 (uni.c).
 * - Menu items are owner-drawn through the same path, so they can show
 *   scripts the process code page cannot.  Options -> "Unicode menus" can
 *   be switched off: items then get ULS-converted plain text ('?' for
 *   characters that do not exist in the process code page).
 *
 * Command line:  pmunitext [-plain] [-lang code] [-log]
 */

#define INCL_DOS
#define INCL_WIN
#define INCL_GPI
#include <os2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "uni.h"
#include "lang.h"

#define APP_CLASS "PMUniText"
#define MENU_PELS 16
#define CHECK_W   18
#define PAD        6
#define ACCEL_GAP 24
#define NBUILTIN   6

static const char *builtinFaces[NBUILTIN] = {
    "Times New Roman MT 30", "Droid Sans Combined", "Monotype Sans Duospace WT J",
    "Times New Roman WT J", "Droid Sans", "Arial"
};

static HAB   hab;
static HMQ   hmq;
static HWND  hwndFrame, hwndClient, hwndMenu;
static PFNWP oldMenuProc, oldFrameProc;

static LANG  langs[MAX_LANGS];
static int   nlang, cur, enIdx = -1;
static char  exeDir[CCHMAXPATH];

static int   bOwner = 1;          /* owner-drawn Unicode menus */
static int   bAbout = 0;
static int   bLog   = 0;
static int   textPels = 22;
static int   fontChoice = 0;      /* 0 = automatic, else 1..NBUILTIN */
static char  face[FACESIZE] = "Times New Roman MT 30";
static int   faceCovers = 1;
static int   ulsOk = 0;
static FILE *flog;

/* ------------------------------------------------------------------ */

static void logf2(const char *fmt, long a, long b)
{
    if (!bLog) return;
    if (!flog) flog = fopen("pmunitext.log", "a");
    if (flog) { fprintf(flog, fmt, a, b); fflush(flog); }
}

/* text for a key: current language, then English, then the key itself */
static const char *get_text(const char *key, int *n)
{
    const char *p = NULL;

    if (cur >= 0 && cur < nlang) p = lang_get(&langs[cur], key, n);
    if (!p && enIdx >= 0)        p = lang_get(&langs[enIdx], key, n);
    if (!p)                      p = uni_ascii(key, n);
    return p;
}

/* ------------------------------------------------------------------ */
/* font selection                                                      */
/* ------------------------------------------------------------------ */

static void resolve_face(void)
{
    char cand[NBUILTIN + 8][FACESIZE];
    int  ncand = 0, i;
    char sample[512];
    int  ns = 0, n;
    const char *p;

    if (fontChoice > 0) {
        strcpy(cand[ncand++], builtinFaces[fontChoice - 1]);
    } else {
        char tmp[256], *tok;
        strncpy(tmp, langs[cur].fonts, sizeof(tmp) - 1); tmp[sizeof(tmp) - 1] = 0;
        for (tok = strtok(tmp, ","); tok && ncand < 8; tok = strtok(NULL, ",")) {
            while (*tok == ' ') tok++;
            if (*tok) { strncpy(cand[ncand], tok, FACESIZE - 1); cand[ncand][FACESIZE - 1] = 0; ncand++; }
        }
        for (i = 0; i < NBUILTIN; i++) strcpy(cand[ncand++], builtinFaces[i]);
    }

    /* characters the face has to be able to draw */
    p = langs[cur].name;
    if (p) { n = utf8_align(p, langs[cur].nameLen, 200); memcpy(sample, p, n); ns = n; }
    p = get_text("HEADING", &n);
    n = utf8_align(p, n, 200);
    memcpy(sample + ns, p, n); ns += n;
    sample[ns] = 0;

    for (i = 0; i < ncand; i++) {
        if (uni_covers(cand[i], sample, ns)) {
            strcpy(face, cand[i]);
            faceCovers = 1;
            return;
        }
    }
    strcpy(face, cand[0]);          /* nothing covers it: show tofu honestly */
    faceCovers = 0;
}

/* ------------------------------------------------------------------ */
/* menu texts: owner-drawn UTF-8 or ULS-converted plain text           */
/* ------------------------------------------------------------------ */

static const char *label_for(USHORT id, int *n)
{
    static const struct { USHORT id; const char *key; } map[] = {
        { IDM_FILE, "MENU_FILE" },         { IDM_RELOAD, "MENU_RELOAD" },
        { IDM_EXIT, "MENU_EXIT" },         { IDM_LANGUAGE, "MENU_LANGUAGE" },
        { IDM_OPTIONS, "MENU_OPTIONS" },   { IDM_OWNERDRAW, "MENU_OWNERDRAW" },
        { IDM_BIGGER, "MENU_BIGGER" },     { IDM_SMALLER, "MENU_SMALLER" },
        { IDM_FONT, "MENU_FONT" },         { IDM_FONT_AUTO, "MENU_FONT_AUTO" },
        { IDM_HELP, "MENU_HELP" },         { IDM_ABOUT, "MENU_ABOUT" }
    };
    int i;

    if (id >= IDM_LANG_BASE && id < IDM_LANG_BASE + MAX_LANGS) {
        int k = id - IDM_LANG_BASE;
        if (k < nlang && langs[k].name) { if (n) *n = langs[k].nameLen; return langs[k].name; }
        return NULL;
    }
    if (id >= IDM_FONT_1 && id < IDM_FONT_1 + NBUILTIN)
        return uni_ascii(builtinFaces[id - IDM_FONT_1], n);
    for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
        if (map[i].id == id) return get_text(map[i].key, n);
    return NULL;
}

/* accelerator column text; the keys themselves are bound in main.rc (ACCELTABLE) */
static const struct { USHORT id; const char *text; } accels[] = {
    { IDM_RELOAD, "F5" },      { IDM_EXIT, "Ctrl+X" },   { IDM_OWNERDRAW, "Ctrl+U" },
    { IDM_BIGGER, "Ctrl+B" },  { IDM_SMALLER, "Ctrl+S" }, { IDM_ABOUT, "F1" }
};

static const char *accel_for(USHORT id)
{
    int i;
    for (i = 0; i < (int)(sizeof(accels) / sizeof(accels[0])); i++)
        if (accels[i].id == id) return accels[i].text;
    return NULL;
}

/* A label may contain one '~' marking the mnemonic character (as in PM
   text menus): "~File", "Exit (E~)" - for scripts without Latin letters
   use a Latin mnemonic in parentheses, e.g. the Japanese "(~F)". */
typedef struct { char t[600]; int n; int mn; int mnLen; unsigned long mnch; } PLABEL;   /* bytes */

static void parse_label(const char *lab, PLABEL *o)
{
    int i = 0, k = 0;

    o->mn = -1; o->mnLen = 0; o->mnch = 0;
    while (lab[i] && k < 590) {
        if (lab[i] == '~' && o->mn < 0 && lab[i + 1]) {      /* mnemonic: next character */
            int used = 0;
            o->mnch = utf8_decode(lab + i + 1, (int)strlen(lab + i + 1), &used);
            o->mn = k; o->mnLen = used;
            i++;
            continue;
        }
        o->t[k++] = lab[i++];
    }
    o->t[k] = 0;
    o->n = k;
}

static unsigned long upc(unsigned long c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* id of the enabled item of menu window hm whose mnemonic is ch, or -1 */
static int find_mnemonic(HWND hm, USHORT ch, MENUITEM *out)
{
    SHORT cnt = SHORT1FROMMR(WinSendMsg(hm, MM_QUERYITEMCOUNT, 0, 0));
    SHORT pos;

    for (pos = 0; pos < cnt; pos++) {
        SHORT id = SHORT1FROMMR(WinSendMsg(hm, MM_ITEMIDFROMPOSITION, MPFROMSHORT(pos), 0));
        MENUITEM mi;
        const char *lab;
        PLABEL pl;
        int n;

        if (!WinSendMsg(hm, MM_QUERYITEM, MPFROM2SHORT(id, FALSE), MPFROMP(&mi))) continue;
        if ((mi.afStyle & MIS_SEPARATOR) || (mi.afAttribute & MIA_DISABLED)) continue;
        lab = label_for((USHORT)id, &n);
        if (!lab) continue;
        parse_label(lab, &pl);
        if (pl.mn >= 0 && upc(pl.mnch) == upc(ch)) { *out = mi; return id; }
    }
    return -1;
}

static void hook_submenu(HWND h);

static void walk_menu(HWND hm)
{
    SHORT cnt = SHORT1FROMMR(WinSendMsg(hm, MM_QUERYITEMCOUNT, 0, 0));
    SHORT pos;

    for (pos = 0; pos < cnt; pos++) {
        SHORT id = SHORT1FROMMR(WinSendMsg(hm, MM_ITEMIDFROMPOSITION, MPFROMSHORT(pos), 0));
        MENUITEM mi;
        const char *lab;
        int n;

        if (!WinSendMsg(hm, MM_QUERYITEM, MPFROM2SHORT(id, FALSE), MPFROMP(&mi))) continue;
        if (mi.afStyle & MIS_SEPARATOR) continue;
        lab = label_for((USHORT)id, &n);
        if (lab) {
            if (bOwner) {
                mi.afStyle = (USHORT)((mi.afStyle & ~MIS_TEXT) | MIS_OWNERDRAW);
                mi.hItem   = (ULONG)lab;
                WinSendMsg(hm, MM_SETITEM, MPFROM2SHORT(0, FALSE), MPFROMP(&mi));
            } else {
                char txt[256];
                if (mi.afStyle & MIS_OWNERDRAW) {
                    mi.afStyle = (USHORT)((mi.afStyle & ~MIS_OWNERDRAW) | MIS_TEXT);
                    mi.hItem   = 0;
                    WinSendMsg(hm, MM_SETITEM, MPFROM2SHORT(0, FALSE), MPFROMP(&mi));
                }
                uls_to_cp(lab, n, txt, sizeof(txt));
                WinSendMsg(hm, MM_SETITEMTEXT, MPFROMSHORT(id), MPFROMP(txt));
            }
        }
        if (mi.afStyle & MIS_SUBMENU) {
            if (bOwner) hook_submenu(mi.hwndSubMenu);
            walk_menu(mi.hwndSubMenu);
        }
    }
}

static void set_check(USHORT id, int on)
{
    WinSendMsg(hwndMenu, MM_SETITEMATTR, MPFROM2SHORT(id, TRUE),
               MPFROM2SHORT(MIA_CHECKED, on ? MIA_CHECKED : 0));
}

static void rebuild_language_items(void)
{
    MENUITEM mi;
    HWND sub;
    int i;

    if (!WinSendMsg(hwndMenu, MM_QUERYITEM, MPFROM2SHORT(IDM_LANGUAGE, TRUE), MPFROMP(&mi))) return;
    sub = mi.hwndSubMenu;
    WinSendMsg(sub, MM_DELETEITEM, MPFROM2SHORT(IDM_LANG_PLACEHOLDER, FALSE), 0);
    for (i = 0; i < MAX_LANGS; i++)
        WinSendMsg(sub, MM_DELETEITEM, MPFROM2SHORT(IDM_LANG_BASE + i, FALSE), 0);
    for (i = 0; i < nlang; i++) {
        MENUITEM ni;
        memset(&ni, 0, sizeof(ni));
        ni.iPosition = MIT_END;
        ni.afStyle   = MIS_TEXT;
        ni.id        = (USHORT)(IDM_LANG_BASE + i);
        WinSendMsg(sub, MM_INSERTITEM, MPFROMP(&ni), MPFROMP(langs[i].english));
    }
}

static void refresh_menus(void)
{
    int i;

    walk_menu(hwndMenu);
    for (i = 0; i < nlang; i++) set_check((USHORT)(IDM_LANG_BASE + i), i == cur);
    set_check(IDM_OWNERDRAW, bOwner);
    set_check(IDM_FONT_AUTO, fontChoice == 0);
    for (i = 0; i < NBUILTIN; i++) set_check((USHORT)(IDM_FONT_1 + i), fontChoice == i + 1);
    WinSendMsg(hwndFrame, WM_UPDATEFRAME, MPFROMLONG(FCF_MENU), 0);
    WinInvalidateRect(hwndMenu, NULL, TRUE);
}

static void refresh_all(void)
{
    char title[160];

    resolve_face();
    sprintf(title, "PMUniText - %s", langs[cur].english);
    WinSetWindowText(hwndFrame, title);
    refresh_menus();
    WinInvalidateRect(hwndClient, NULL, TRUE);
}

/* ------------------------------------------------------------------ */
/* owner-drawn menu items (subclassed menu window)                     */
/* ------------------------------------------------------------------ */

static MRESULT owner_msg(ULONG msg, MPARAM mp2, int *handled)
{
    *handled = 0;
    switch (msg) {
    case WM_MEASUREITEM: {
        POWNERITEM poi = (POWNERITEM)PVOIDFROMMP(mp2);
        HPS hps;
        ULONG ocp;
        int w = 0, h = MENU_PELS + 6;
        const char *lab = (const char *)poi->hItem;
        PLABEL pl;
        const char *ac;

        if (!lab) break;
        parse_label(lab, &pl);
        ac = (poi->hwnd == hwndMenu) ? NULL : accel_for((USHORT)poi->idItem);
        hps = WinGetPS(poi->hwnd);
        if (uni_begin(hps, face, MENU_PELS, &ocp)) {
            w = uni_width(hps, pl.t, pl.n);
            if (ac) { int an; const char *au = uni_ascii(ac, &an); w += ACCEL_GAP + uni_width(hps, au, an); }
            h = uni_height(hps) + 4;
        }
        uni_end(hps, ocp);
        WinReleasePS(hps);
        w += 2 * PAD + (poi->hwnd == hwndMenu ? 0 : CHECK_W);
        poi->rclItem.xLeft = 0; poi->rclItem.yBottom = 0;
        poi->rclItem.xRight = w; poi->rclItem.yTop = h;
        logf2("measure item %ld -> %ld wide\n", (long)poi->idItem, (long)w);
        *handled = 1;
        return MRFROMSHORT((SHORT)h);
    }

    case WM_DRAWITEM: {
        POWNERITEM poi = (POWNERITEM)PVOIDFROMMP(mp2);
        const char *lab = (const char *)poi->hItem;
        HPS hps;
        ULONG ocp;
        LONG bg, fg;
        int hil, chk, dis, bar, itemH, lineH, desc;
        POINTL pt;
        PLABEL pl;
        const char *ac;

        if (!lab) break;
        parse_label(lab, &pl);
        ac = (poi->hwnd == hwndMenu) ? NULL : accel_for((USHORT)poi->idItem);
        hil = (poi->fsAttribute & MIA_HILITED) != 0;
        chk = (poi->fsAttribute & MIA_CHECKED) != 0;
        dis = (poi->fsAttribute & MIA_DISABLED) != 0;
        bar = (poi->hwnd == hwndMenu);
        bg = hil ? SYSCLR_MENUHILITEBGND : SYSCLR_MENU;
        fg = dis ? SYSCLR_MENUDISABLEDTEXT : (hil ? SYSCLR_MENUHILITE : SYSCLR_MENUTEXT);

        hps = WinGetPS(poi->hwnd);
        WinFillRect(hps, &poi->rclItem, bg);
        GpiSetColor(hps, fg);
        itemH = (int)(poi->rclItem.yTop - poi->rclItem.yBottom);
        if (uni_begin(hps, face, MENU_PELS, &ocp)) {
            long tx, by;

            lineH = uni_height(hps);
            desc  = uni_descender(hps);
            tx = poi->rclItem.xLeft + PAD + (bar ? 0 : CHECK_W);
            by = poi->rclItem.yBottom + (itemH - lineH) / 2 + desc;

            uni_draw(hps, tx, by, pl.t, pl.n);
            if (pl.mn >= 0) {                        /* underline the mnemonic */
                long ux = tx + uni_width(hps, pl.t, pl.mn);
                long uw = uni_width(hps, pl.t + pl.mn, pl.mnLen);
                pt.x = ux;      pt.y = by - 2; GpiMove(hps, &pt);
                pt.x = ux + uw; pt.y = by - 2; GpiLine(hps, &pt);
            }
            if (ac) {                                /* accelerator, right aligned */
                int an;
                const char *au = uni_ascii(ac, &an);
                uni_draw(hps, poi->rclItem.xRight - PAD - uni_width(hps, au, an), by, au, an);
            }
        }
        uni_end(hps, ocp);
        if (chk && !bar) {                       /* check mark */
            long x = poi->rclItem.xLeft + 4, y = poi->rclItem.yBottom + itemH / 2;
            pt.x = x;      pt.y = y;      GpiMove(hps, &pt);
            pt.x = x + 4;  pt.y = y - 5;  GpiLine(hps, &pt);
            pt.x = x + 11; pt.y = y + 5;  GpiLine(hps, &pt);
        }
        WinReleasePS(hps);
        poi->fsAttribute    &= ~(MIA_CHECKED | MIA_HILITED | MIA_FRAMED | MIA_DISABLED);
        poi->fsAttributeOld &= ~(MIA_CHECKED | MIA_HILITED | MIA_FRAMED | MIA_DISABLED);
        logf2("draw item %ld hil=%ld\n", (long)poi->idItem, (long)hil);
        *handled = 1;
        return MRFROMLONG(TRUE);
    }
    }
    return 0;
}

/* PM sends WM_MEASUREITEM / WM_DRAWITEM to the OWNER of the menu (the
   frame); the menu window itself is subclassed too in case a submenu
   delivers them there. */
static MRESULT EXPENTRY MenuProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == WM_MEASUREITEM || msg == WM_DRAWITEM) {
        int h;
        MRESULT r = owner_msg(msg, mp2, &h);
        logf2("menu proc msg %lx handled %ld\n", (long)msg, (long)h);
        if (h) return r;
    }
    return oldMenuProc(hwnd, msg, mp1, mp2);
}

/* open a top-level menu (Alt+mnemonic) or run a submenu item (plain key) */
static void open_top(USHORT id)
{
    WinSendMsg(hwndMenu, MM_STARTMENUMODE, MPFROM2SHORT(TRUE, FALSE), 0);
    WinSendMsg(hwndMenu, MM_SELECTITEM, MPFROM2SHORT(id, FALSE), MPFROM2SHORT(0, FALSE));
}

static void activate_item(HWND hm, USHORT id, const MENUITEM *mi)
{
    if (mi->afStyle & MIS_SUBMENU) {
        WinSendMsg(hm, MM_SELECTITEM, MPFROM2SHORT(id, FALSE), MPFROM2SHORT(0, FALSE));
    } else {
        WinSendMsg(hwndMenu, MM_ENDMENUMODE, MPFROMSHORT(TRUE), 0);
        WinPostMsg(hwndClient, WM_COMMAND, MPFROM2SHORT(id, CMDSRC_MENU), MPFROM2SHORT(CMDSRC_MENU, FALSE));
    }
}

static PFNWP oldSubProc;
static HWND  hooked[16];
static int   nhooked;

static MRESULT EXPENTRY SubMenuProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == WM_CHAR && bOwner) {
        USHORT fs = SHORT1FROMMP(mp1);
        if ((fs & KC_CHAR) && !(fs & KC_KEYUP) && !(fs & (KC_ALT | KC_CTRL))) {
            MENUITEM mi;
            int id = find_mnemonic(hwnd, SHORT1FROMMP(mp2), &mi);
            if (id >= 0) { activate_item(hwnd, (USHORT)id, &mi); return MRFROMLONG(TRUE); }
        }
    }
    return oldSubProc(hwnd, msg, mp1, mp2);
}

static void hook_submenu(HWND h)
{
    int i;
    PFNWP p;

    for (i = 0; i < nhooked; i++) if (hooked[i] == h) return;
    if (nhooked >= 16) return;
    p = WinSubclassWindow(h, SubMenuProc);
    if (p) { if (!oldSubProc) oldSubProc = p; hooked[nhooked++] = h; }
}

static MRESULT EXPENTRY FrameProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (msg == WM_MEASUREITEM || msg == WM_DRAWITEM) {
        int h;
        MRESULT r = owner_msg(msg, mp2, &h);
        logf2("frame proc msg %lx handled %ld\n", (long)msg, (long)h);
        if (h) return r;
    }
    return oldFrameProc(hwnd, msg, mp1, mp2);
}

/* ------------------------------------------------------------------ */
/* client area                                                         */
/* ------------------------------------------------------------------ */

/* largest k (1..len) with width(p[0..k)) <= maxw */
static int fit_chars(HPS hps, const char *p, int len, int maxw)
{
    int lo = utf8_align(p, len, 1), hi = len;           /* at least one character */

    if (uni_width(hps, p, len) <= maxw) return len;
    while (lo < hi) {
        int mid = utf8_align(p, len, (lo + hi + 1) / 2);   /* always on a character boundary */
        if (mid <= lo) break;
        if (uni_width(hps, p, mid) <= maxw) lo = mid; else hi = utf8_align(p, len, mid - 1);
    }
    return lo;
}

/* wrap one paragraph (no line breaks inside) into lines of at most maxw pels */
static void draw_block(HPS hps, int x, int *top, int maxw, const char *p, int len)
{
    int lineH = uni_height(hps), desc = uni_descender(hps);

    while (len > 0) {
        int fit = fit_chars(hps, p, len, maxw), brk = fit, draw;

        if (fit < len) {
            int i, sp = 0;
            for (i = fit; i > 0; i--) if (p[i - 1] == ' ') { sp = i; break; }
            if (sp > 0) brk = sp;               /* word break; CJK breaks anywhere */
        }
        draw = brk;
        while (draw > 0 && p[draw - 1] == ' ') draw--;
        uni_draw(hps, x, *top - (lineH - desc), p, draw);
        *top -= lineH;
        p += brk; len -= brk;
    }
}

/* paragraph-aware wrapper: splits at 0x000A and wraps each piece */
static void draw_text(HPS hps, int x, int *top, int maxw, const char *s, int n)
{
    int i = 0;

    while (i <= n) {
        int j = i;
        while (j < n && s[j] != 0x0A) j++;
        if (j == i) *top -= uni_height(hps);
        else        draw_block(hps, x, top, maxw, s + i, j - i);
        i = j + 1;
    }
}

static void paint_client(HWND hwnd)
{
    HPS hps;
    RECTL rcl, rc;
    ULONG ocp;
    int top, margin = 14, maxw, n, k;
    char info[300];
    const char *p;
    char key[16];

    hps = WinBeginPaint(hwnd, NULLHANDLE, &rcl);
    WinQueryWindowRect(hwnd, &rc);
    WinFillRect(hps, &rc, SYSCLR_WINDOW);
    GpiSetColor(hps, CLR_BLACK);
    maxw = (int)(rc.xRight - rc.xLeft) - 2 * margin;
    top = (int)rc.yTop - margin;

    if (uni_begin(hps, face, textPels * 3 / 2, &ocp)) {
        p = get_text("HEADING", &n);
        draw_text(hps, margin, &top, maxw, p, n);
        top -= 6;
    }
    uni_end(hps, ocp);

    if (uni_begin(hps, face, textPels, &ocp)) {
        for (k = 1; k <= 6; k++) {
            const char *t;
            sprintf(key, "TEXT%d", k);
            t = cur >= 0 ? lang_get(&langs[cur], key, &n) : NULL;
            if (!t && enIdx >= 0) t = lang_get(&langs[enIdx], key, &n);
            if (!t) continue;
            draw_text(hps, margin, &top, maxw, t, n);
            top -= 8;
        }
        if (bAbout) {
            top -= 6;
            p = get_text("ABOUT", &n);
            draw_text(hps, margin, &top, maxw, p, n);
        }
    }
    uni_end(hps, ocp);

    /* diagnostics (ASCII, drawn through the same path) */
    if (uni_begin(hps, face, 13, &ocp)) {
        int lh = uni_height(hps), y = (int)rc.yBottom + 6 + lh;
        sprintf(info, "file %s  |  %ld bytes UTF-8 = %ld chars, %ld invalid", langs[cur].path,
                langs[cur].bytes, langs[cur].chars, langs[cur].invalid);
        p = uni_ascii(info, &n);
        uni_draw(hps, margin, y - lh + uni_descender(hps), p, n);
        sprintf(info, "face \"%s\" at %d pels, glyph coverage %s  |  ULS %s (CP %lu)  |  menus %s",
                face, textPels, faceCovers ? "ok" : "MISSING (tofu)",
                ulsOk ? "on" : "off", (unsigned long)uls_codepage(),
                bOwner ? "owner-drawn UCS-2 (CP 1200)" : "plain text via ULS");
        p = uni_ascii(info, &n);
        uni_draw(hps, margin, y + 2 + uni_descender(hps), p, n);
    }
    uni_end(hps, ocp);
    WinEndPaint(hps);
}

/* ------------------------------------------------------------------ */

static MRESULT EXPENTRY ClientProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    switch (msg) {
    case WM_PAINT:
        paint_client(hwnd);
        return 0;

    case WM_CHAR: {
        USHORT fs = SHORT1FROMMP(mp1);
        logf2("client WM_CHAR fs=%lx ch=%lx\n", (long)fs, (long)SHORT1FROMMP(mp2));
        if (bOwner && (fs & KC_ALT) && !(fs & (KC_KEYUP | KC_CTRL)) && SHORT1FROMMP(mp2) > 0x20) {   /* Alt+letter: KC_CHAR is not set */
            MENUITEM mi;
            int id = find_mnemonic(hwndMenu, SHORT1FROMMP(mp2), &mi);
            if (id >= 0) { open_top((USHORT)id); return MRFROMLONG(TRUE); }
        }
        break;
    }

    case WM_COMMAND: {
        USHORT id = SHORT1FROMMP(mp1);
        if (id >= IDM_LANG_BASE && id < IDM_LANG_BASE + MAX_LANGS && id - IDM_LANG_BASE < nlang) {
            cur = id - IDM_LANG_BASE;
            refresh_all();
            return 0;
        }
        if (id >= IDM_FONT_1 && id < IDM_FONT_1 + NBUILTIN) { fontChoice = id - IDM_FONT_1 + 1; refresh_all(); return 0; }
        switch (id) {
        case IDM_EXIT:     WinPostMsg(hwnd, WM_QUIT, 0, 0); break;
        case IDM_RELOAD: {
            char code[24];
            int i;
            strcpy(code, langs[cur].code);
            lang_free(langs, nlang);
            nlang = lang_scan(exeDir, langs, MAX_LANGS);
            enIdx = -1; cur = 0;
            for (i = 0; i < nlang; i++) {
                if (!stricmp(langs[i].code, "en")) enIdx = i;
                if (!stricmp(langs[i].code, code)) cur = i;
            }
            rebuild_language_items();
            refresh_all();
            break;
        }
        case IDM_OWNERDRAW: bOwner = !bOwner; refresh_all(); break;
        case IDM_BIGGER:    if (textPels < 56) textPels += 2; WinInvalidateRect(hwnd, NULL, TRUE); break;
        case IDM_SMALLER:   if (textPels > 10) textPels -= 2; WinInvalidateRect(hwnd, NULL, TRUE); break;
        case IDM_FONT_AUTO: fontChoice = 0; refresh_all(); break;
        case IDM_ABOUT:     bAbout = !bAbout; WinInvalidateRect(hwnd, NULL, TRUE); break;
        }
        return 0;
    }
    }
    return WinDefWindowProc(hwnd, msg, mp1, mp2);
}

static void find_exe_dir(void)
{
    PTIB ptib;
    PPIB ppib;
    char *p;

    exeDir[0] = 0;
    DosGetInfoBlocks(&ptib, &ppib);
    if (DosQueryModuleName(ppib->pib_hmte, sizeof(exeDir), exeDir) == 0) {
        p = strrchr(exeDir, '\\');
        if (p) p[1] = 0; else exeDir[0] = 0;
    }
}

int main(int argc, char **argv)
{
    QMSG qmsg;
    ULONG flCreate = FCF_TITLEBAR | FCF_SYSMENU | FCF_MENU | FCF_MINMAX |
                     FCF_SIZEBORDER | FCF_TASKLIST | FCF_SHELLPOSITION | FCF_ACCELTABLE;
    char want[24] = "en", langdir[CCHMAXPATH];
    int i;

    for (i = 1; i < argc; i++) {
        if (!stricmp(argv[i], "-plain")) bOwner = 0;
        else if (!stricmp(argv[i], "-log")) bLog = 1;
        else if (!stricmp(argv[i], "-lang") && i + 1 < argc) { strncpy(want, argv[++i], 23); want[23] = 0; }
    }

    hab = WinInitialize(0);
    hmq = WinCreateMsgQueue(hab, 0);
    find_exe_dir();
    ulsOk = uls_init();

    strcpy(langdir, exeDir);
    strcat(langdir, "lang\\");
    strcpy(exeDir, langdir);              /* from here on exeDir = language folder */
    nlang = lang_scan(exeDir, langs, MAX_LANGS);
    if (nlang == 0) {
        WinMessageBox(HWND_DESKTOP, HWND_DESKTOP, "No lang\\*.txt files found next to the program.",
                      "PMUniText", 0, MB_OK | MB_ERROR | MB_MOVEABLE);
        return 1;
    }
    for (i = 0; i < nlang; i++) {
        if (!stricmp(langs[i].code, "en")) enIdx = i;
        if (!stricmp(langs[i].code, want)) cur = i;
    }

    WinRegisterClass(hab, APP_CLASS, ClientProc, CS_SIZEREDRAW, 0);
    hwndFrame = WinCreateStdWindow(HWND_DESKTOP, 0, &flCreate, APP_CLASS, "PMUniText",
                                   0, NULLHANDLE, ID_MAIN, &hwndClient);
    if (!hwndFrame) return 1;
    hwndMenu = WinWindowFromID(hwndFrame, FID_MENU);
    oldMenuProc = WinSubclassWindow(hwndMenu, MenuProc);
    oldFrameProc = WinSubclassWindow(hwndFrame, FrameProc);
    rebuild_language_items();
    refresh_all();
    WinSetWindowPos(hwndFrame, HWND_TOP, 60, 60, 760, 560,
                    SWP_SIZE | SWP_MOVE | SWP_SHOW | SWP_ACTIVATE);

    while (WinGetMsg(hab, &qmsg, NULLHANDLE, 0, 0)) WinDispatchMsg(hab, &qmsg);

    WinDestroyWindow(hwndFrame);
    lang_free(langs, nlang);
    uni_done();
    if (flog) fclose(flog);
    WinDestroyMsgQueue(hmq);
    WinTerminate(hab);
    return 0;
}
