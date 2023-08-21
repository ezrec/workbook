WB_NAME=AmiBench
WB_VERSION=46
WB_REVISION=1
WB_ABOUT="(C) Copyright 2023 AmigaKit Ltd.\n\nAmibench uses code from the AROS project written by Jason McMullan"

RM=rm -f

VBCC=/opt/vbcc
VBCC_CONFIG=aos68k

AMIGA_NDK=$(VBCC)/targets/m68k-amigaos/NDK_3.9

INCLUDES=$(VBCC)/targets/m68k-amigaos/include $(AMIGA_NDK)/Include/include_h
DEFINES=-DWB_NAME='$(WB_NAME)' -DWB_VERSION=$(WB_VERSION) -DWB_REVISION=$(WB_REVISION) -DWB_ABOUT='$(WB_ABOUT)'

CFLAGS=+$(VBCC_CONFIG) -g -O0 -DDEBUG=0 \
	   $(patsubst %,-I%,$(INCLUDES)) $(DEFINES)
LINKOPTS=-L$(AMIGA_NDK)/Include/linker_libs -ldebug -lamigas

HDRS=$(wildcard *.h)
SRCS=main.c  wbapp.c  wbicon.c  wbset.c  wbvirtual.c  wbwindow.c  workbook.c  workbook_intern.c
OBJS=$(patsubst %.c,%.o,$(SRCS))

all: LoadWB

clean:
	$(RM) LoadWB $(OBJS)

%.o: %.c $(HDRS)
	vc $(CFLAGS) -c $<

LoadWB: $(OBJS)
	vc $(CFLAGS) -o $@ $(OBJS) $(LINKOPTS)

release.zip: LoadWB
	$(MKDIR) System
	$(CP) LoadWB System/$(WB_NAME)
	$(ZIP) -r $@ System/ README.md
	$(RM) -r System

release: release.zip
