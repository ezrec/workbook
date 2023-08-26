/*
    Copyright (C) 2011, The AROS Development Team. All rights reserved.

    Desc: Workbook headers
*/

#ifndef WORKBOOK_H
#define WORKBOOK_H

#ifndef WB_NAME
#define WB_NAME     Workbook
#endif
#ifndef WB_ABOUT
#define WB_ABOUT     Copyright © 2023 The AROS Project
#endif
#ifndef WB_VERSION
#define WB_VERSION      1
#define WB_REVISION     5
#endif

#define _D(x)
#define _AS_STRING(x)   #x
#define AS_STRING(x) _AS_STRING(x)

#ifdef __AROS__
#include <dos/bptr.h>
#include <exec/rawfmt.h>
#include <proto/workbench.h>
#include <workbench/handler.h>
#else
#include <proto/wb.h>
#include <workbench/icon.h>
#endif
#include <intuition/classes.h>
#include <intuition/intuition.h>

#ifndef __AROS__
#include "workbook_vbcc.h"
#endif // !__AROS__

struct WorkbookBase {
    APTR wb_IntuitionBase;
    APTR wb_DOSBase;
    APTR wb_UtilityBase;
    APTR wb_GadToolsBase;
    APTR wb_IconBase;
    APTR wb_WorkbenchBase;
    APTR wb_GfxBase;
    APTR wb_LayersBase;

    Class  *wb_WBApp;
    Class  *wb_WBWindow;
    Class  *wb_WBVirtual;
    Class  *wb_WBIcon;
    Class  *wb_WBSet;

    Object *wb_App;

    /* Create a new task that simply OpenWorkbenchObject()'s
     * it's argment.
     */
    BPTR wb_OpenerSegList;

    // Execute... command buffer
    char ExecuteBuffer[128+1];
};

/* FIXME: Remove these #define xxxBase hacks
   Do not use this in new code !
*/
#define IntuitionBase wb->wb_IntuitionBase
#define DOSBase       wb->wb_DOSBase
#define UtilityBase   wb->wb_UtilityBase
#define GadToolsBase  wb->wb_GadToolsBase
#define IconBase      wb->wb_IconBase
#define WorkbenchBase wb->wb_WorkbenchBase
#define GfxBase       wb->wb_GfxBase
#define LayersBase    wb->wb_LayersBase

extern struct ExecBase *SysBase;

#include <string.h>
#include <proto/exec.h>

typedef IPTR (*wbPopupActionFunc)(struct WorkbookBase *wb, CONST_STRPTR input, APTR arg);

IPTR wbPopupAction(struct WorkbookBase *wb,
                         CONST_STRPTR title,
                         CONST_STRPTR description,
                         CONST_STRPTR request,
                         STRPTR saveBuffer, // Can be NULL,
                         LONG saveBufferSize, // Can be 0
                         CONST_STRPTR forbidden,
                         wbPopupActionFunc action,
                         APTR arg);
VOID wbPopupIoErr(struct WorkbookBase *wb, CONST_STRPTR title, LONG ioerr, CONST_STRPTR prefix);
struct Region *wbClipWindow(struct WorkbookBase *wb, struct Window *win);
void wbUnclipWindow(struct WorkbookBase *wb, struct Window *win, struct Region *clip);
ULONG WorkbookMain(void);

#endif /* WORKBOOK_H */
