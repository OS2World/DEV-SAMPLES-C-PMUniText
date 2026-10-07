@echo off
set WATCOM=C:\WATCOM
set OS2TK=C:\OS2TK45
set PATH=%WATCOM%\binp;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h;%OS2TK%\h
set LIB=%WATCOM%\lib386;%WATCOM%\lib386\os2;%OS2TK%\lib
wcl386 -bt=os2 -l=os2v2_pm -i=%OS2TK%\h probe.c -fe=probe.exe > build-probe.log 2>&1
type build-probe.log
