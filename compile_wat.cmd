@echo off
rem Build PMUniText with Open Watcom (run from the project root)
setlocal
if "%WATCOM%"=="" set WATCOM=C:\WATCOM
if "%OS2TK%"=="" set OS2TK=C:\OS2TK45
set PATH=%WATCOM%\binp;%WATCOM%\binw;%PATH%
set INCLUDE=%WATCOM%\h;%OS2TK%\h
set LIB=%WATCOM%\lib386;%WATCOM%\lib386\os2;%OS2TK%\lib
if not exist bin-wat md bin-wat
wmake -f makefile.wat clean > compile_wat.log 2>&1
wmake -f makefile.wat all >> compile_wat.log 2>&1
type compile_wat.log
if exist bin-wat\pmunitext.exe (echo BUILD OK) else (echo BUILD FAILED)
endlocal
