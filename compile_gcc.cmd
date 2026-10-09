@echo off
rem Build PMUniText with GCC / kLIBC (run from the project root)
rem setlocal/endlocal keep the settings below out of the caller's environment
setlocal
rem make needs a shell it can find: use cmd.exe (fix by Dave Yeo)
set MAKESHELL=cmd.exe
rem link with wlink; the default ilink gave warnings and a hanging executable
set EMXOMFLD_TYPE=WLINK
set EMXOMFLD_LINKER=wl.exe
set EMXOMFLD_PRELINK=0
if not exist bin-gcc md bin-gcc
make -f makefile.gcc 2>&1 | tee compile_gcc.log
if exist bin-gcc\pmunitext.exe (echo BUILD OK) else (echo BUILD FAILED)
endlocal
