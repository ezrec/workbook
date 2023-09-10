/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Application Class
*/

#include <string.h>
#include <limits.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/layers.h>

#include <dos/dostags.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>

#include "workbook_intern.h"
#include "workbook_menu.h"
#include "classes.h"

struct wbApp {
    struct Screen  *Screen;
    struct MsgPort *WinPort;
    ULONG           WinMask;   /* Mask of our port(s) */
    struct MsgPort *AppPort;
    ULONG           AppMask;   /* Mask of our port(s) */
    struct MsgPort *NotifyPort;
    ULONG           NotifyMask;   /* Mask of our port(s) */
    Object         *Root;      /* Background 'root' window */
    struct MinList  Windows; /* Subwindows */

    // Execute... command buffer
    char ExecuteBuffer[128+1];

    // Drag 'n Drop management
    Object *DragDrop;
    BOOL    DragDropActive;

    // On-intitick actions
    struct {
        BOOL DragDrop;
    } OnIntuiTick;
};

static void wbOpenDrawer(Class *cl, Object *obj, CONST_STRPTR path)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *win;

    win = NewObject(WBWindow, NULL,
                        WBWA_Path, path,
                        WBWA_UserPort, my->WinPort,
                        WBWA_NotifyPort, my->NotifyPort,
                        WBWA_Screen, my->Screen,
                        TAG_END);

    if (win)
    {
        DoMethod(obj, OM_ADDMEMBER, win);
    }
}

static struct Window *wbAppWindowAt(Class *cl, Object *obj, struct Screen *screen) {
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct Layer *layer = NULL;

    LockLayerInfo(&screen->LayerInfo);
    layer = WhichLayer(&screen->LayerInfo, screen->MouseX, screen->MouseY);
    UnlockLayerInfo(&screen->LayerInfo);

    if (layer != NULL) {
        return (struct Window *)layer->Window;
    }

    return NULL;
}

// OM_NEW
static IPTR WBApp__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my;
    IPTR rc;

    // Required attributes
    struct Screen *screen = (struct Screen *)GetTagData(WBAA_Screen, (IPTR)NULL, ops->ops_AttrList);
    if (screen == NULL) {
        return 0;
    }

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return 0;

    my = INST_DATA(cl, rc);

    NEWLIST(&my->Windows);

    // Set our screen.
    my->Screen = screen;

    /* Create our Workbench message port */
    my->AppPort = CreatePort(NULL, 0);

    if (my->AppPort == NULL) {
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    /* Create our Window message port */
    my->WinPort = CreatePort(NULL, 0);

    if (my->WinPort == NULL) {
        DeleteMsgPort(my->AppPort);
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    /* Create our Notify message port */
    my->NotifyPort = CreatePort(NULL, 0);

    if (my->NotifyPort == NULL) {
        DeleteMsgPort(my->WinPort);
        DeleteMsgPort(my->AppPort);
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    my->AppMask |= (1UL << my->AppPort->mp_SigBit);
    my->WinMask |= (1UL << my->WinPort->mp_SigBit);
    my->NotifyMask |= (1UL << my->NotifyPort->mp_SigBit);

    // Initialize our DragDrop information
    my->DragDrop = NewObject(WBDragDrop, NULL, WBDA_Screen, my->Screen, TAG_END);
    if (my->DragDrop == NULL) {
        DeleteMsgPort(my->NotifyPort);
        DeleteMsgPort(my->WinPort);
        DeleteMsgPort(my->AppPort);
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    /* Create our root window */
    my->Root = NewObject(WBWindow, NULL,
                         WBWA_Path, NULL,
                         WBWA_Screen, my->Screen,
                         WBWA_UserPort, my->WinPort,
                         WBWA_NotifyPort, my->NotifyPort,
                         TAG_END);
    if (my->Root == NULL) {
        DisposeObject(my->DragDrop);
        DeleteMsgPort(my->NotifyPort);
        DeleteMsgPort(my->WinPort);
        DeleteMsgPort(my->AppPort);
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    DoMethod(my->Root, OM_ADDTAIL, &my->Windows);

    return rc;
}

// OM_DISPOSE
static IPTR WBApp__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *tstate = (Object *)my->Windows.mlh_Head;
    Object *tmp;

    /* Get rid of the subwindows */
    while ((tmp = NextObject(&tstate))) {
        STACKED ULONG omrmethodID;
        omrmethodID = OM_REMOVE;
        DoMethodA(tmp, (Msg)&omrmethodID);
        DisposeObject(obj);
    }

    // Get rid of the DragDrop manager
    DisposeObject(my->DragDrop);

    DeleteMsgPort(my->NotifyPort);
    DeleteMsgPort(my->AppPort);
    DeleteMsgPort(my->WinPort);

    return DoSuperMethodA(cl, obj, msg);
}

// OM_ADDMEMBER
static IPTR WBApp__OM_ADDMEMBER(Class *cl, Object *obj, struct opMember *opm)
{
    struct wbApp *my = INST_DATA(cl, obj);

    return DoMethod(opm->opam_Object, OM_ADDTAIL, &my->Windows);
}

// OM_REMMEMBER
static IPTR WBApp__OM_REMMEMBER(Class *cl, Object *obj, struct opMember *opm)
{
    return DoMethod(opm->opam_Object, OM_REMOVE);
}


// Find WBWindow object, given a Window pointer.
static Object *wbLookupWindow(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;
    struct Window *match = NULL;

    /* Is it the root window? */
    while ((owin = NextObject(&ostate))) {
        match = NULL;
        GetAttr(WBWA_Window, owin, (IPTR *)&match);
        if (match == win) {
            return owin;
        }
    }

    return NULL;
}

static void wbRefreshWindow(Class *cl, Object *obj, struct Window *win)
{
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        STACKED ULONG wbrefmethodID;
        wbrefmethodID = WBWM_Refresh;
        DoMethodA(owin, (Msg)&wbrefmethodID);
    }
}

static void wbNewSizeWindow(Class *cl, Object *obj, struct Window *win)
{
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        STACKED ULONG wbnewsmethodID;
        wbnewsmethodID = WBWM_NewSize;
        DoMethodA(owin, (Msg)&wbnewsmethodID);
    }
}

static void wbCloseWindow(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        struct opMember opmmsg;
        opmmsg.MethodID = OM_REMMEMBER;
        opmmsg.opam_Object = owin;
        DoMethodA(obj, (Msg)&opmmsg);
        DisposeObject(owin);
    }
}

// Function to display the "About" pop-up
static void wbAbout(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct EasyStruct es = {
       .es_StructSize = sizeof(es),
       .es_Flags = 0,
       .es_Title = AS_STRING(WB_NAME),
       .es_TextFormat = "Release V%ld.%ld\n%s",
       .es_GadgetFormat = "Ok",
    };

    struct {
	    ULONG version;
	    ULONG revision;
	    CONST_STRPTR about;
    } args = {
	    WB_VERSION,
	    WB_REVISION,
	    AS_STRING(WB_ABOUT),
    };
    EasyRequestArgs(0, &es, 0, (RAWARG)&args);
}

static IPTR execute_command(struct WorkbookBase *wb, CONST_STRPTR command, APTR dummy) {
    SystemTags(command,
            SYS_Asynch, TRUE,
                        SYS_Input, NULL,
                        SYS_Output, SYS_DupStream,
                        TAG_END);

    return 0;
}

// Broadcast a message to all selected icons in all windows.
//
// Returns a count of selected icons.
static IPTR WBApp__WBAM_ForSelected(Class *cl, Object *obj, struct wbam_ForSelected *wbamf)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    D(bug("%s: MethodID: 0x%08lx\n", __func__, (IPTR)wbamf->wbamf_Msg->MethodID));

    // Broadcast to all child windows.
    IPTR rc = 0;
    while ((owin = NextObject(&ostate)) != NULL) {
        IPTR count;
        count = DoMethod(owin, WBWM_ForSelected, wbamf->wbamf_Msg);
        D(
                IPTR path;
                GetAttr(WBWA_Path, owin, &path);
                bug("%s: selected: %s(%ld)\n", __func__, (CONST_STRPTR)path, (IPTR)count);
        );
        if (count) {
            DoMethod(owin, WBWM_Refresh);
        }
        rc += count;
    }
    D(bug("\n"));

    return rc;
}

#ifdef __AROS__
#define wbAppForSelected(cl, obj, ...) ({ \
    IPTR  msg[] = { AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) }; \
    struct wbam_ForSelected wbamf = { \
        .MethodID = WBAM_ForSelected, \
	.wbamf_Msg = (Msg)msg, \
	}; \
    IPTR rc = WBApp__WBAM_ForSelected(cl, obj, &wbamf);\
    rc; })
#else
static IPTR wbAppForSelected(Class *cl, Object *obj, ULONG method, ...)
{
    struct wbam_ForSelected msg = {
        .MethodID = WBAM_ForSelected,
        .wbamf_Msg = (Msg)&method,
    };

    return WBApp__WBAM_ForSelected(cl, obj, &msg);
}
#endif

// Clear all selections.
//
static IPTR WBApp__WBAM_ClearSelected(Class *cl, Object *obj, Msg msg)
{
    struct TagItem attrlist[] = {
        { GA_Selected, FALSE },
        { TAG_END, },
    };
    wbAppForSelected(cl, obj, OM_SET, attrlist, NULL);

    return 0;
}

// Report selections (as a WBOPENA_* tag list)
static IPTR WBApp__WBAM_ReportSelected(Class *cl, Object *obj, struct wbam_ReportSelected *wbamr)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);

    ULONG total_tags = 0;

    Object *ostate;
    Object *owin;

    // Count all children
    ostate = (Object *)my->Windows.mlh_Head;
    while ((owin = NextObject(&ostate)) != NULL) {
        IPTR rc = DoMethod(owin, WBWM_ReportSelected, NULL);
        D(bug("%s: %lx => %ld tags\n", __func__, owin, rc));
        // Account for the TAG_END at the end of the report.
        total_tags += rc - 1;
    }
    total_tags++;

    D(bug("%s: %ld total tags\n", __func__, total_tags));

    // Did they just want to know how big the report was?
    if (wbamr->wbamr_ReportTags == NULL) {
        return total_tags;
    }

    // Allocate the data to send back.
    struct TagItem *ti = AllocateTagItems(total_tags);

    ULONG index = 0;
    // Add all children
    ostate = (Object *)my->Windows.mlh_Head;
    while ((owin = NextObject(&ostate)) != NULL) {
        struct TagItem *ti_ptr = NULL;
        IPTR rc = DoMethod(owin, WBWM_ReportSelected, &ti_ptr);
        D(bug("%s: %lx => %ld tags\n", __func__, owin, rc));
        for (IPTR n = 0; n < rc; n++, index++) {
            ti[index] = ti_ptr[n];
        }
        // Account for the TAG_END at the end of the report.
        index--;
        FreeVec(ti_ptr);
    }

    ti[index++].ti_Tag = TAG_END;

    D(bug("%s: %ld reported tags\n", __func__, index));

    D(if (index != total_tags) bug("%s: Expected %ld tags, got %ld tags!!!!\n", __func__, total_tags, index));

    *(wbamr->wbamr_ReportTags) = ti;

    return total_tags;
}

static BOOL wbMenuPick(Class *cl, Object *obj, struct Window *win, UWORD menuNumber)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *owin;
    struct MenuItem *item;
    BOOL quit = FALSE;

    owin = wbLookupWindow(cl, obj, win);

    D(bug("Menu: %lx\n", (IPTR)menuNumber));
    while (menuNumber != MENUNULL) {
        ULONG handled = FALSE;

        item = ItemAddress(win->MenuStrip, menuNumber);

        /* Let the window have first opportunity */
        if (owin)
        {
            struct wbwm_MenuPick wbmpmsg;
            wbmpmsg.MethodID = WBWM_MenuPick;
            wbmpmsg.wbwmp_MenuItem = item;
            wbmpmsg.wbwmp_MenuNumber = menuNumber;
            handled = DoMethodA(owin, (Msg)&wbmpmsg);
        }

        if (!handled) {
            LONG rc;
            struct EasyStruct es = {
                .es_StructSize = sizeof(es),
                .es_Title = (STRPTR)"Shutdown",
                .es_TextFormat = (STRPTR)"System is ready to shut down.",
#ifdef __AROS__
                .es_GadgetFormat = "Shutdown|Reboot",
#else
                .es_GadgetFormat = "Reboot",
#endif
            };
            switch (WBMENU_ITEM_ID(item)) {
            case WBMENU_ID(WBMENU_WB_CUST_UPDATER):
                OpenWorkbenchObject("SYS:System/Updater", TAG_END);
                break;
            case WBMENU_ID(WBMENU_WB_CUST_AMISTORE):
                OpenWorkbenchObject("SYS:Utilities/Amistore", TAG_END);
                break;
            case WBMENU_ID(WBMENU_WB_QUIT):
                quit = TRUE;
                break;
            case WBMENU_ID(WBMENU_WB_ABOUT):
                wbAbout(cl, obj, win);
                break;
            case WBMENU_ID(WBMENU_WB_EXECUTE):
                wbPopupAction(wb, "Execute a file",
                                  "Enter Command and its Arguments",
                                  "Command:",
                                  my->ExecuteBuffer, sizeof(my->ExecuteBuffer),
                                  NULL,
                                  execute_command, NULL);
                break;
            case WBMENU_ID(WBMENU_WB_SHUTDOWN):
                rc = EasyRequest(NULL, &es, NULL, TAG_END);
#ifdef __AROS__
                /* Does the user wants a shutdown or reboot? */
                if (rc == 1) {
                    ShutdownA(SD_ACTION_POWEROFF);
                } else {
                    /* Try to reboot. */
                    ShutdownA(SD_ACTION_COLDREBOOT);
                }
#endif
                // If all else fails, ColdReboot()
                if (rc || !rc) {
                    ColdReboot();
                }
                break;
            case WBMENU_ID(WBMENU_IC_OPEN):
                wbAppForSelected(cl, obj, WBIM_Open);
                DoMethod(obj, WBAM_ClearSelected);
                break;
            case WBMENU_ID(WBMENU_IC_COPY):
                wbAppForSelected(cl, obj, WBIM_Copy);
                break;
            case WBMENU_ID(WBMENU_IC_RENAME):
                wbAppForSelected(cl, obj, WBIM_Rename);
                break;
            case WBMENU_ID(WBMENU_IC_INFO):
                wbAppForSelected(cl, obj, WBIM_Info);
                break;
            case WBMENU_ID(WBMENU_IC_SNAPSHOT):
                wbAppForSelected(cl, obj, WBIM_Snapshot);
                break;
            case WBMENU_ID(WBMENU_IC_UNSNAPSHOT):
                wbAppForSelected(cl, obj, WBIM_Unsnapshot);
                break;
            case WBMENU_ID(WBMENU_IC_LEAVE_OUT):
                wbAppForSelected(cl, obj, WBIM_Leave_Out);
                break;
            case WBMENU_ID(WBMENU_IC_PUT_AWAY):
                wbAppForSelected(cl, obj, WBIM_Put_Away);
                break;
            case WBMENU_ID(WBMENU_IC_DELETE):
                wbAppForSelected(cl, obj, WBIM_Delete);
                break;
            case WBMENU_ID(WBMENU_IC_FORMAT):
                wbAppForSelected(cl, obj, WBIM_Format);
                break;
            case WBMENU_ID(WBMENU_IC_EMPTY_TRASH):
                wbAppForSelected(cl, obj, WBIM_Empty_Trash);
                break;
            }
        }

        menuNumber = item->NextSelect;
    }

    return quit;
}

static void wbIntuiTick(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *owin;

    if (my->OnIntuiTick.DragDrop) {
        my->OnIntuiTick.DragDrop = FALSE;
        // Find the WBWindow (we own) that we dropped into.
        struct Window *win = wbAppWindowAt(cl, obj, my->Screen);
        BOOL ok = FALSE;
        if (win) {
            // See if this matches any of our owned windows.
            Object *wbwin = wbLookupWindow(cl, obj, win);

            if (wbwin) {
                ULONG windowX = my->Screen->MouseX - win->LeftEdge;
                ULONG windowY = my->Screen->MouseY - win->TopEdge;
                ok = DoMethod(wbwin, WBxM_DragDropped, NULL, windowX, windowY);
            }
        }
        if (ok) {
            // Update all windows
            D(bug("%s: Update all windows...\n", __func__));
            Object *ostate = (Object *)my->Windows.mlh_Head;
            Object *owin;

            while ((owin = NextObject(&ostate))) {
                DoMethod(owin, WBWM_InvalidateContents);
                DoMethod(owin, WBWM_IntuiTick);
            }
         }
    }

    if ((owin = wbLookupWindow(cl, obj, win))) {
        DoMethod(owin, WBWM_IntuiTick);
    }
}

static void wbForAllWindows(Class *cl, Object *obj, ULONG method)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    while ((owin = NextObject(&ostate))) {
        DoMethod(owin, method);
    }
}

static void wbHideAllWindows(Class *cl, Object *obj)
{
    wbForAllWindows(cl, obj, WBWM_Hide);
}

static void wbShowAllWindows(Class *cl, Object *obj)
{
    wbForAllWindows(cl, obj, WBWM_Show);
}

static void wbCloseAllWindows(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    while ((owin = NextObject(&ostate))) {
        DoMethod(obj, OM_REMMEMBER, owin);
        DisposeObject(owin);
    }
}

// WBAM_Workbench - Register and handle all workbench events
static IPTR WBApp__WBAM_Workbench(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    ULONG done = FALSE;

    CurrentDir(BNULL);

    if (RegisterWorkbench(my->AppPort)) {
        while (!done) {
            ULONG mask;

            mask = Wait(my->AppMask | my->WinMask | my->NotifyMask);

            if (mask & my->AppMask) {
                struct WBHandlerMessage *wbhm;
                while ((wbhm = (APTR)GetMsg(my->AppPort)) != NULL) {
                    D(bug("%s: AppPort Msg %lx\n", __func__, (IPTR)wbhm->wbhm_Type));
                    switch (wbhm->wbhm_Type) {
                    case WBHM_TYPE_SHOW:
                        /* Show all windows */
                        wbShowAllWindows(cl, obj);
                        break;
                    case WBHM_TYPE_HIDE:
                        /* Hide all windows */
                        wbHideAllWindows(cl, obj);
                        break;
                    case WBHM_TYPE_OPEN:
                        /* Open a drawer */
                        wbOpenDrawer(cl, obj, wbhm->wbhm_Data.Open.Name);
                        break;
                    case WBHM_TYPE_UPDATE:
                        /* Refresh an open window/object */
                        break;
                    }

                    ReplyMsg((APTR)wbhm);
                }
            }

            if (mask & my->WinMask) {
                struct IntuiMessage *im;

                while ((im = GT_GetIMsg(my->WinPort)) != NULL) {
                    switch (im->Class) {
                    case IDCMP_CLOSEWINDOW:
                        /* Dispose the window */
                        wbCloseWindow(cl, obj, im->IDCMPWindow);
                        break;
                    case IDCMP_REFRESHWINDOW:
                        /* call WBWM_Refresh on the window */
                        wbRefreshWindow(cl, obj, im->IDCMPWindow);
                        break;
                    case IDCMP_NEWSIZE:
                        /* call WBWM_NewSize on the window */
                        wbNewSizeWindow(cl, obj, im->IDCMPWindow);
                        break;
                    case IDCMP_MENUPICK:
                        done = wbMenuPick(cl, obj, im->IDCMPWindow, im->Code);
                        break;
                    case IDCMP_INTUITICKS:
                        wbIntuiTick(cl, obj, im->IDCMPWindow);
                        break;
                    default:
                        D(bug("im=%lx, Class=%ld, Code=%ld\n", (IPTR)im, (IPTR)im->Class, (IPTR)im->Code));
                        break;
                    }

                    GT_ReplyIMsg(im);
                }
            }

            if (mask & my->NotifyMask) {
                struct NotifyMessage *nm;
                BOOL invalidated = FALSE;
                D(bug("%s: Notify messages waiting.\n", __func__));
                while ((nm = (APTR)GetMsg(my->NotifyPort)) != NULL) {
                    Object *wbwin = (Object *)nm->nm_NReq->nr_UserData;
                    D(bug("%s: Notfied: %lx\n", __func__, (IPTR)wbwin));
                    DoMethod(wbwin, WBWM_InvalidateContents);
                    invalidated = TRUE;
                }

                if (invalidated) {
                    wbForAllWindows(cl, obj, WBWM_CacheContents);
                }
            }

         }

        wbCloseAllWindows(cl, obj);

        UnregisterWorkbench(my->AppPort);
    }

    return FALSE;
}

static IPTR WBApp__WBAM_DragDropBegin(Class *cl, Object *obj, Msg msg)
{
    struct wbApp *my = INST_DATA(cl, obj);

    if (my->DragDropActive) {
        CoerceMethod(cl, obj, WBDM_End);
    }

    // Add all selected images.
    IPTR rc = wbAppForSelected(cl, obj, WBIM_DragDropAdd, NULL, my->DragDrop);
    D(bug("%s: %ld selected for DragDrop\n", __func__, rc));

    if (rc > 0) {
        // Begin drag/drop
        DoMethod(my->DragDrop, WBDM_Begin);

        my->DragDropActive = TRUE;
    }

    return rc;
}

static IPTR WBApp__WBAM_DragDropUpdate(Class *cl, Object *obj, Msg msg)
{
    struct wbApp *my = INST_DATA(cl, obj);

    if (my->DragDropActive) {
        DoMethod(my->DragDrop, WBDM_Update);
    }

    return my->DragDropActive;
}

static IPTR WBApp__WBAM_DragDropEnd(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);

    if (my->DragDropActive) {
        // End the current drag/drop
        my->OnIntuiTick.DragDrop = DoMethod(my->DragDrop, WBDM_End);
        // Clear any existing DnD images.
        DoMethod(my->DragDrop, WBDM_Clear);

        my->DragDropActive = FALSE;
    }

    return 0;
}


static IPTR WBApp_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    METHOD_CASE(WBApp, OM_NEW);
    METHOD_CASE(WBApp, OM_DISPOSE);
    METHOD_CASE(WBApp, OM_ADDMEMBER);
    METHOD_CASE(WBApp, OM_REMMEMBER);
    METHOD_CASE(WBApp, WBAM_Workbench);
    METHOD_CASE(WBApp, WBAM_DragDropBegin);
    METHOD_CASE(WBApp, WBAM_DragDropUpdate);
    METHOD_CASE(WBApp, WBAM_DragDropEnd);
    METHOD_CASE(WBApp, WBAM_ForSelected);
    METHOD_CASE(WBApp, WBAM_ClearSelected);
    METHOD_CASE(WBApp, WBAM_ReportSelected);
    default:           rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBApp_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "rootclass", NULL,
                    sizeof(struct wbApp),
                    0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBApp_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
