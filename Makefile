WB_NAME="AmiBench"
WB_VERSION=46
WB_REVISION=1
WB_ABOUT="Copyright © 2023 AmigaKit Ltd.\\n\\nAmiBench uses code from the AROS project.\nDeveloped by Jason S. McMullan"
DEBUG=0

RM=rm -f
CP=cp
MKDIR=mkdir -p
ZIP=zip

VBCC=/opt/vbcc
VBCC_CONFIG=aos68k

AMIGA_NDK=$(VBCC)/targets/m68k-amigaos/NDK_3.9

INCLUDES=$(VBCC)/targets/m68k-amigaos/include $(AMIGA_NDK)/Include/include_h
DEFINES=-DWB_NAME='$(WB_NAME)' -DWB_VERSION=$(WB_VERSION) -DWB_REVISION=$(WB_REVISION) -DWB_ABOUT='$(WB_ABOUT)'

ifeq ($(DEBUG),1)
	DFLAGS := -g -O0 -DDEBUG=1
else
	DFLAGS := -O0 -DEBUG=0 -dontwarn=64
endif

CFLAGS=+$(VBCC_CONFIG) $(DFLAGS) \
	   -warn=-1 \
	   -dontwarn=65 -dontwarn=81 \
	   -dontwarn=163 -dontwarn=166 -dontwarn=167 \
	   -dontwarn=307 -dontwarn=306 -warnings-as-errors \
	   $(patsubst %,-I%,$(INCLUDES)) $(DEFINES)
LINKOPTS=-L$(AMIGA_NDK)/Include/linker_libs -ldebug -lamigas

HDRS=$(wildcard *.h)
SRCS=main.c  wbapp.c  wbicon.c  wbset.c  wbvirtual.c  wbwindow.c wbcurrent.c workbook.c  workbook_intern.c
OBJS=$(patsubst %.c,%.o,$(SRCS))

all: Workbook

clean:
	$(RM) Workbook $(OBJS) release.zip

%.o: %.c $(HDRS)
	vc $(CFLAGS) -c $<

Workbook: $(OBJS)
	vc $(CFLAGS) -o $@ $(OBJS) $(LINKOPTS)

release.zip: Workbook
	$(MKDIR) System
	$(CP) Workbook System/$(WB_NAME)
	$(ZIP) -r $@ System/ README.md
	$(RM) -r System

release: release.zip
