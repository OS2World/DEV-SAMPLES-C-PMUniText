@echo off
rem Build PMUniText with GCC / kLIBC (run from the project root)
set EMXOMFLD_TYPE=WLINK
set EMXOMFLD_LINKER=wl.exe
set EMXOMFLD_PRELINK=0
if not exist bin-gcc md bin-gcc
make -f makefile.gcc 2>&1 | tee compile_gcc.log
if exist bin-gcc\pmunitext.exe (echo BUILD OK) else (echo BUILD FAILED)
