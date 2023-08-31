/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Main
*/

#include <stdio.h>

#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/exec.h>
#include <proto/icon.h>

#include "workbook_intern.h"
#include "classes.h"

/* Allocate classes and run the main app */
static int WB_Main(struct WorkbookBase *wb)
{
    int rc = RETURN_OK;

    wb->wb_WBApp = WBApp_MakeClass(wb);
    if (!wb->wb_WBApp)
        goto exit;

    wb->wb_WBWindow = WBWindow_MakeClass(wb);
    if (!wb->wb_WBWindow)
        goto exit;

    wb->wb_WBVirtual = WBVirtual_MakeClass(wb);
    if (!wb->wb_WBVirtual)
        goto exit;

    wb->wb_WBSet    = WBSet_MakeClass(wb);
    if (!wb->wb_WBSet)
        goto exit;

    wb->wb_WBIcon   = WBIcon_MakeClass(wb);
    if (!wb->wb_WBIcon)
        goto exit;

    wb->wb_App = NewObject(WBApp, NULL, TAG_END);
    if (wb->wb_App) {
        STACKED ULONG wbmethodID;
        wbmethodID = WBAM_Workbench;
        DoMethodA(wb->wb_App, (Msg)&wbmethodID);
        DisposeObject(wb->wb_App);
        rc = 0;
    }

exit:
    if (wb->wb_WBIcon)
        FreeClass(wb->wb_WBIcon);
    if (wb->wb_WBSet)
        FreeClass(wb->wb_WBSet);
    if (wb->wb_WBVirtual)
        FreeClass(wb->wb_WBVirtual);
    if (wb->wb_WBWindow)
        FreeClass(wb->wb_WBWindow);
    if (wb->wb_WBApp)
        FreeClass(wb->wb_WBApp);

    return rc;
}

#undef WorkbenchBase
#undef DOSBase

/* This wrapper is needed, so that we can start
 * workbench items from an Input handler
 */
static AROS_PROCH(wbOpener, argstr, argsize, SysBase)
{
    AROS_PROCFUNC_INIT

    APTR WorkbenchBase = OpenLibrary("workbench.library", 0);
    if (WorkbenchBase) {
        /* 'argstr' is already an absolute path */
        D(bug("%s: OpenWorkbenchObject(%s)\n", __func__, argstr));
        OpenWorkbenchObject(argstr, TAG_END);
        CloseLibrary(WorkbenchBase);
    }
    D(bug("%s: Close\n", __func__));

    return 0;

    AROS_PROCFUNC_EXIT
}

ULONG WorkbookMain(void)
{
    struct WorkbookBase *wb;
    APTR DOSBase;
    int rc = RETURN_ERROR;

    D(bug("%s: Starting up...\n", AS_STRING(WB_NAME)));

    wb = NULL;

    wb = AllocVec(sizeof(*wb), MEMF_ANY | MEMF_CLEAR);
    if (!wb)
        goto error;

    wb->wb_DOSBase = OpenLibrary("dos.library", 0);
    if (wb->wb_DOSBase == NULL)
        goto error;

    DOSBase = wb->wb_DOSBase;

    wb->wb_IntuitionBase = OpenLibrary("intuition.library",0);
    if (wb->wb_IntuitionBase == NULL)
        goto error;

    wb->wb_UtilityBase = OpenLibrary("utility.library",0);
    if (wb->wb_UtilityBase == NULL)
        goto error;

    wb->wb_GadToolsBase = OpenLibrary("gadtools.library",0);
    if (wb->wb_GadToolsBase == NULL)
        goto error;

    /* Version 44 or later for DrawIconStateA */
    wb->wb_IconBase = OpenLibrary("icon.library",44);
    if (wb->wb_IconBase == NULL)
        goto error;

    /* Version 45 or later for RegisterWorkbench */
    wb->wb_WorkbenchBase = OpenLibrary("workbench.library",45);
    if (wb->wb_WorkbenchBase == NULL)
        goto error;

    wb->wb_GfxBase = OpenLibrary("graphics.library",0);
    if (wb->wb_GfxBase == NULL)
        goto error;

    wb->wb_LayersBase = OpenLibrary("layers.library", 0);
    if (wb->wb_LayersBase == NULL)
        goto error;

    wb->wb_OpenerSegList = CreateSegList((APTR)wbOpener);
    if (wb->wb_OpenerSegList == BNULL)
        goto error;

    // Set process and task name to "Workbench", for old AmigaOS tools
    SetProgramName("Workbench");
    struct Task *task = FindTask(NULL);
    STRPTR oldName = task->tc_Node.ln_Name;
    task->tc_Node.ln_Name = (STRPTR)"Workbench";

    SetConsoleTask(NULL);
    rc = WB_Main(wb);

    // Restore (possibly allocated) task name.
    task->tc_Node.ln_Name = oldName;

    UnLoadSeg(wb->wb_OpenerSegList);

error:
    if (wb) {
        if (wb->wb_LayersBase)
            CloseLibrary(wb->wb_LayersBase);

        if (wb->wb_GfxBase)
            CloseLibrary(wb->wb_GfxBase);

        if (wb->wb_WorkbenchBase)
            CloseLibrary(wb->wb_WorkbenchBase);

        if (wb->wb_IconBase)
            CloseLibrary(wb->wb_IconBase);

        if (wb->wb_GadToolsBase)
            CloseLibrary(wb->wb_GadToolsBase);

        if (wb->wb_IntuitionBase)
            CloseLibrary(wb->wb_GadToolsBase);

        if (wb->wb_DOSBase)
            CloseLibrary(wb->wb_DOSBase);

        FreeVec(wb);
    }

    return rc;
}


