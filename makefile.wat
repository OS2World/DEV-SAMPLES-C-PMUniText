# OpenWatcom makefile for PMUniText
#
# Tools: wcc386, wlink, wrc, wmake.  Run from the project root (compile_wat.cmd).
# Output goes to bin-wat\ (the exe looks for lang\ next to itself).

SRC = src
BIN = bin-wat

!ifndef OS2TK
OS2TK = C:\OS2TK45
!endif

CFLAGS = -bt=os2 -w3 -zq -d0 -ox -i=$(OS2TK)\h -i=$(SRC)

HEADERS = $(SRC)\main.h $(SRC)\uni.h $(SRC)\lang.h

all : $(BIN)\pmunitext.exe copylang .SYMBOLIC

$(BIN)\main.obj : $(SRC)\main.c $(HEADERS)
	wcc386 $(CFLAGS) -fo=$@ $(SRC)\main.c

$(BIN)\uni.obj : $(SRC)\uni.c $(HEADERS)
	wcc386 $(CFLAGS) -fo=$@ $(SRC)\uni.c

$(BIN)\lang.obj : $(SRC)\lang.c $(HEADERS)
	wcc386 $(CFLAGS) -fo=$@ $(SRC)\lang.c

$(BIN)\main.res : $(SRC)\main.rc $(SRC)\main.h
	wrc -r -q -bt=os2 -i=$(OS2TK)\h -i=$(SRC) -fo=$@ $(SRC)\main.rc

$(BIN)\pmunitext.exe : $(BIN)\main.obj $(BIN)\uni.obj $(BIN)\lang.obj $(BIN)\main.res
	wlink system os2v2_pm option quiet option map=$(BIN)\pmunitext.map name $@ file $(BIN)\main.obj, $(BIN)\uni.obj, $(BIN)\lang.obj
	wrc -q -bt=os2 $(BIN)\main.res $@

copylang : .SYMBOLIC
	@if not exist $(BIN)\lang md $(BIN)\lang
	@copy lang\*.txt $(BIN)\lang > nul

clean : .SYMBOLIC
	@if exist $(BIN)\*.obj del $(BIN)\*.obj
	@if exist $(BIN)\*.res del $(BIN)\*.res
	@if exist $(BIN)\*.map del $(BIN)\*.map
	@if exist $(BIN)\pmunitext.exe del $(BIN)\pmunitext.exe
