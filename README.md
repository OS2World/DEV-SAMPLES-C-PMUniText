# DEV-SAMPLES-C-PMUniText (PMUniText)

Prototype: **Unicode text (including Japanese, Chinese, Korean, Russian, Greek) in a plain
Presentation Manager program** - no Qt, no SDL, no external libraries. Text comes from UTF-8
language files that sit next to the executable.

![Japanese menus](img/PMUniText_menu_ja.png)
![Korean](img/PMUniText_ko.png)

## How it works

| Piece | Where | What it does |
|---|---|---|
| UTF-8 language files | `lang\*.txt` | `KEY=value`, `\n` = line break, `#` comment; added/edited without recompiling |
| UTF-8 text | `src\lang.c` `utf8_sanitize()` | text stays UTF-8 (code page 1208) in memory; BOM skipped, malformed sequences become U+FFFD |
| Drawing | `src\uni.c` `uni_begin/uni_draw` | logical font with `usCodePage = 1208`, `GpiSetCp(hps, 1208)`, `GpiCharStringAt` with the length in bytes - GPI reads the UTF-8 directly, no UCS-2 conversion |
| Menus | `src\main.c` | every item switched to `MIS_OWNERDRAW`; the UTF-8 label is the item handle (`hItem`) and is painted with the same drawing code |
| Plain fallback | `uls_to_cp()` | UTF-8 -> process code page through ULS (`UCONV.DLL`, loaded at run time); `?` for characters that do not exist in the code page |
| Font choice | `resolve_face()` | per-language `FONT=` list, then built-in candidates; a face is accepted only if it really draws the language's characters (rendered into a memory bitmap and compared with the face's "missing glyph") |

Language menu items are labelled with each language's own name (`LANG_NAME`), so every script is
visible in the menu at once.

## Findings (ArcaOS 5.1, tested in the VM)

* **GPI accepts UTF-8 directly: code page 1208.** A font created with `FATTRS.usCodePage = 1208` (and
  `GpiSetCp(hps, 1208)`) draws UTF-8 bytes exactly like the same text as UCS-2 in code page 1200
  (identical pels for Latin, Greek, Cyrillic, Japanese, Korean; `probe\probe1208.c`, `probe\probe1208.log`).
  The *font's* code page decides: a font created for 1200 followed by `GpiSetCp(1208)` draws garbage.
  (The first version of this sample converted to UCS-2 and used 1200; thanks to Dave Yeo for pointing
  out CP 1208.)
* GPI length arguments are in **bytes** (the UTF-8 length; 2 per character for code page 1200).
* 4-byte (non-BMP) sequences are accepted and do not crash, but the tested fonts have no glyphs for
  them (they draw as two missing-glyph boxes, i.e. GPI turns them into a surrogate pair).
* Faces that contain Kana, Kanji/Hanzi and Hangul glyphs: `Droid Sans Combined`,
  `Times New Roman MT 30`, `Monotype Sans Duospace WT J`, `Times New Roman WT J`.
  Latin/Greek/Cyrillic: most of the installed outline fonts. Full list: `probe\probe.log`.
* **PM sends `WM_MEASUREITEM` / `WM_DRAWITEM` of a menu to the frame (the menu's owner), not to the
  menu window.** Subclassing only the menu window gives an empty menu bar.
* The title bar and standard controls cannot show Unicode. The title therefore uses the ASCII
  English language name (`LANG_ENGLISH`); everything else that must be localized is drawn.
* Plain (non-owner-drawn) menus work but show `?` for anything outside code page 850. Switch
  between both modes in *Options* to see the difference.

## Mnemonics and accelerators

* **Mnemonics:** put one `~` before the letter in a `MENU_*` value, like a normal PM menu
  (`MENU_FILE=~File`). For scripts without Latin letters use the Asian convention and add the
  Latin letter in parentheses (`MENU_FILE=ファイル(~F)`). The mnemonic is underlined in the
  owner-drawn item; Alt+letter opens a top-level menu and a plain letter runs an item of an open
  submenu (PM only does this for text items, so `main.c` does it for owner-drawn ones). In
  plain-text mode PM handles the `~` itself.
* **Accelerators:** keys are bound in the `ACCELTABLE` in `src\main.rc` (F5 reload, Ctrl+X exit,
  Ctrl+U Unicode menus, Ctrl+B larger, Ctrl+S smaller, F1 about). The key names shown right-aligned
  in the menu come from the `accels[]` table in `main.c`, so keep both in sync.
* PM does not set `KC_CHAR` for Alt+letter `WM_CHAR` messages; check `KC_ALT` and the character code.

## Limits of the prototype

* Only the BMP is usable in practice: GPI accepts non-BMP UTF-8 (emoji, rare CJK ideographs) but the
  installed fonts have no glyphs for it. No right-to-left or complex shaping (Arabic, Hebrew,
  Indic) - GPI does no layout.
* No text input / IME. Entry fields, MLEs and list boxes still use the process code page.
* IPF help (`wipfc`) is not Unicode; a Unicode help text would need its own viewer.

## Command line

`pmunitext [-plain] [-lang code] [-log]`   (`-log` writes `pmunitext.log` with the owner-draw messages)

## Build (both toolchains)

* Open Watcom: `compile_wat.cmd` -> `bin-wat\pmunitext.exe` (+ `bin-wat\lang\`)
* GCC / kLIBC: `compile_gcc.cmd` -> `bin-gcc\pmunitext.exe` (+ `bin-gcc\lang\`)

## Layout

```
src\       main.c (window, menus, painting)  uni.c (UTF-8 helpers, ULS, fonts, drawing)  lang.c (language files)
lang\      en es ru el ja zh_CN zh_TW ko
img\       screenshots
probe\     probe.c + probe.log  (what GPI does with code page 1200 on this system)
           probe1208.c + probe1208.log  (GPI with UTF-8 / code page 1208)
```

## License

BSD 3-Clause (see `LICENSE`), same as the PM Template.

## Release notes

* 0.3 - text is drawn as UTF-8 through GPI code page 1208; the UTF-8 to UCS-2 conversion for drawing is gone (UCS-2 is only used inside the ULS fallback).
* 0.2 - mnemonics (underlined, Alt+letter and letters in open submenus) and accelerator column for owner-drawn menus.
* 0.1 - first prototype (October 2026).
