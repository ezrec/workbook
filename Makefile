WB_NAME=AmiBench
WB_VERSION=46
WB_REVISION=1
WB_ABOUT="(C) Copyright 2023 AmigaKit Ltd.\n\nAmibench uses code from the AROS project written by Jason McMullan"

RM=rm -f
CP=cp
MKDIR=mkdir -p
ZIP=zip

VBCC=/opt/vbcc
VBCC_CONFIG=aos68k

AMIGA_NDK=$(VBCC)/targets/m68k-amigaos/NDK_3.9

INCLUDES=$(VBCC)/targets/m68k-amigaos/include $(AMIGA_NDK)/Include/include_h
DEFINES=-DWB_NAME='\"$(WB_NAME)\"' -DWB_VERSION=$(WB_VERSION) -DWB_REVISION=$(WB_REVISION) -DWB_ABOUT='\"$(WB_ABOUT)\"'

CFLAGS=+$(VBCC_CONFIG) -O0 $(patsubst %,-I%,$(INCLUDES)) $(DEFINES)

HDRS=$(wildcard *.h)
LOADWB_SRCS=main.c  wbapp.c  wbicon.c  wbset.c  wbvirtual.c  wbwindow.c  workbook.c  workbook_intern.c
LOADWB_OBJS=$(patsubst %.c,%.o,$(LOADWB_SRCS))

all: LoadWB

clean:
	$(RM) LoadWB $(LOADWB_OBJS)

%.o: %.c $(HDRS)
	vc $(CFLAGS) -c $<

LoadWB: $(LOADWB_OBJS)
	vc $(CFLAGS) -o $@ $(LOADWB_OBJS) -lamigas

release.zip: LoadWB
	$(MKDIR) System
	$(CP) LoadWB System/$(WB_NAME)
	$(ZIP) -r $@ System/ README.md

release: release.zip
