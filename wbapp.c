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

#include <dos/dostags.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <libraries/gadtools.h>

#include "workbook_intern.h"
#include "workbook_menu.h"
#include "classes.h"

struct wbApp {
    struct MsgPort *WinPort;
    ULONG           WinMask;   /* Mask of our port(s) */
    struct MsgPort *AppPort;
    ULONG           AppMask;   /* Mask of our port(s) */
    Object         *Root;      /* Background 'root' window */
    struct MinList  Windows; /* Subwindows */
};

static void wbOpenDrawer(Class *cl, Object *obj, CONST_STRPTR path)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *win;

    win = NewObject(WBWindow, NULL,
                        WBWA_UserPort, my->WinPort,
                        WBWA_Path, path,
                        TAG_END);

    if (win)
    {
        struct opMember opmmsg;
        opmmsg.MethodID = OM_ADDMEMBER;
        opmmsg.opam_Object = win;
        DoMethodA(obj, (Msg)&opmmsg);
    }
}

// OM_NEW
static IPTR WBAppNew(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return 0;

    my = INST_DATA(cl, rc);

    NEWLIST(&my->Windows);

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

    my->AppMask |= (1UL << my->AppPort->mp_SigBit);
    my->WinMask |= (1UL << my->WinPort->mp_SigBit);

    /* Create our root window */
    my->Root = NewObject(WBWindow, NULL,
                         WBWA_Path, NULL,
                         WBWA_UserPort, my->WinPort,
                         TAG_END);

    if (my->Root == NULL) {
        DeleteMsgPort(my->WinPort);
        DeleteMsgPort(my->AppPort);
        DoSuperMethod(cl, (Object *)rc, OM_DISPOSE);
        return 0;
    }

    return rc;
}

// OM_DISPOSE
static IPTR WBAppDispose(Class *cl, Object *obj, Msg msg)
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

    /* Get rid of the main window */
    DisposeObject(my->Root);

    DeleteMsgPort(my->AppPort);
    DeleteMsgPort(my->WinPort);

    return DoSuperMethodA(cl, obj, msg);
}

// OM_ADDMEMBER
static IPTR WBAppAddMember(Class *cl, Object *obj, struct opMember *opm)
{
    struct opAddTail omatmsg;
    struct wbApp *my = INST_DATA(cl, obj);

    omatmsg.MethodID = OM_ADDTAIL;
    omatmsg.opat_List = (struct List *)&my->Windows;

    return DoMethodA(opm->opam_Object, (Msg)&omatmsg);
}

// OM_REMMEMBER
static IPTR WBAppRemMember(Class *cl, Object *obj, struct opMember *opm)
{
    STACKED ULONG omrmethodID;
    omrmethodID = OM_REMOVE;
    return DoMethodA(opm->opam_Object, (Msg)&omrmethodID);
}


static Object *wbLookupWindow(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;
    struct Window *match = NULL;

    /* Is it the root window? */
    GetAttr(WBWA_Window, my->Root, (IPTR *)&match);
    if (match == win)
        return my->Root;

    while ((owin = NextObject(&ostate))) {
        match = NULL;
        GetAttr(WBWA_Window, owin, (IPTR *)&match);
        if (match == win)
            return owin;
    }

    return NULL;
}

static void wbRefreshWindow(Class *cl, Object *obj, struct Window *win)
{
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        STACKED ULONG wbrefmethodID;
        wbrefmethodID = WBWM_REFRESH;
        DoMethodA(owin, (Msg)&wbrefmethodID);
    }
}

static void wbNewSizeWindow(Class *cl, Object *obj, struct Window *win)
{
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        STACKED ULONG wbnewsmethodID;
        wbnewsmethodID = WBWM_NEWSIZE;
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

    EasyRequest(0, &es, 0, WB_VERSION, WB_REVISION, (IPTR)AS_STRING(WB_ABOUT));
}

static void execute_command(struct WorkbookBase *wb, const char *command) {
    SystemTags(command, SYS_Asynch, TRUE, 
			SYS_Input, NULL,
			SYS_Output, SYS_DupStream, TAG_DONE);
}

static void wbExecute(Class *cl, Object *obj, struct Window *win)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct Window *executeWindow = NULL;
    struct Gadget *glist = NULL, *gctx, *inputGadget=NULL;
    enum {
	   textField,
	   inputField,
	   executeButton,
	   cancelButton
    };
    struct NewGadget newGadget = {0};
    const char *inputBuffer;

    const int winWidth = 400;
    const int winHeight = 100;

    struct Screen *screen = NULL;
    void *vi = NULL;
    
    screen = LockPubScreen(NULL);
    if (screen == NULL) {
	    goto exit;
    }

    vi = GetVisualInfo(screen, TAG_END);
    if (vi == NULL) {
	    goto exit;
    }

    gctx = CreateContext(&glist);
    if (gctx == NULL) {
	    goto exit;
    }

    LONG top_border = screen->WBorTop + (screen->Font->ta_YSize + 1);

    // Message
    newGadget.ng_TopEdge = top_border + 8;
    newGadget.ng_LeftEdge = 20;
    newGadget.ng_VisualInfo = vi;
    newGadget.ng_Width = winWidth - 50;
    newGadget.ng_Height = 12;
    newGadget.ng_GadgetText = (UBYTE *)""; // Initialize with empty string
    newGadget.ng_GadgetID = textField;
    newGadget.ng_Flags = NG_HIGHLABEL;
    gctx = CreateGadget(TEXT_KIND, gctx, &newGadget, GTTX_Text, "Enter command and its arguments:", TAG_DONE);
 
    // Create an input field for the command
    newGadget.ng_TopEdge += 20;
    newGadget.ng_Height = 18;
    newGadget.ng_GadgetText = (UBYTE *)""; // Initialize with empty string
    newGadget.ng_GadgetID = inputField;
    inputGadget = gctx = CreateGadget(STRING_KIND, gctx, &newGadget, GTST_String, wb->ExecuteBuffer,
		   GTST_MaxChars, sizeof(wb->ExecuteBuffer)-1,
		   GACT_RELVERIFY, TRUE,
		   TAG_DONE);

    if (gctx == NULL) {
	D(bug("No input field!"));
	goto exit;
    }
    inputBuffer = ((struct StringInfo *)(gctx->SpecialInfo))->Buffer;

    // Create the "Execute" button
    newGadget.ng_TopEdge += 24;
    newGadget.ng_Width = 60;
    newGadget.ng_Height = 20;
    newGadget.ng_GadgetText = "_Execute";
    newGadget.ng_GadgetID = executeButton;
    gctx = CreateGadget(BUTTON_KIND, gctx, &newGadget, GT_Underscore, '_', TAG_DONE);

    if (gctx == NULL) {
	D(bug("No execute button!"));
	goto exit;
    }

    // Create the "Cancel" button
    newGadget.ng_Width = 60;
    newGadget.ng_LeftEdge = winWidth - 20 - newGadget.ng_Width;
    newGadget.ng_GadgetText = "_Cancel";
    newGadget.ng_GadgetID = cancelButton;
    gctx = CreateGadget(BUTTON_KIND, gctx, &newGadget, GT_Underscore, '_', TAG_DONE);

    if (gctx == NULL) {
	D(bug("No cancel button!"));
	goto exit;
    }

    // Create a simple window for the dialog
    executeWindow = OpenWindowTags(NULL,
        WA_Left, 20,
        WA_Top, 20,
        WA_Width, winWidth,
        WA_Height, winHeight,
        WA_Title, "Execute Command",
	WA_Gadgets, glist,
	WA_PubScreen, screen,
	WA_AutoAdjust, TRUE,
	WA_SimpleRefresh, TRUE,
        WA_DepthGadget, TRUE,
        WA_DragBar, TRUE,
        WA_Activate, TRUE,
        WA_CloseGadget, TRUE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_REFRESHWINDOW | STRINGIDCMP | BUTTONIDCMP | IDCMP_VANILLAKEY,
        TAG_DONE);

    if (executeWindow == NULL) {
	goto exit;
    }

    GT_RefreshWindow(executeWindow, NULL);

    // Activate text entry.
    ActivateGadget(inputGadget, executeWindow, NULL);

    // Event loop
    BOOL done = FALSE;
    ULONG signals = 1L << executeWindow->UserPort->mp_SigBit;

    while (!done) {
        ULONG result = Wait(signals);

        if (result & signals) {
            struct IntuiMessage *msg;

            while ((msg = GT_GetIMsg(executeWindow->UserPort)) != NULL) {
		struct Gadget *gad = (struct Gadget *)msg->IAddress;

		ULONG msgClass = msg->Class;
		UWORD msgCode = msg->Code;
                GT_ReplyIMsg(msg);

		switch (msgClass) {
		case IDCMP_REFRESHWINDOW:
		    GT_BeginRefresh(executeWindow);
		    GT_EndRefresh(executeWindow, TRUE);
		    break;
		case IDCMP_CLOSEWINDOW:
                    done = TRUE;
		    break;
		case IDCMP_VANILLAKEY:
		    D(bug("msgCode: %d\n", msgCode));
		    switch (msgCode) {
		    case '\r':
			// fallthrough
		    case 'e':
			execute_command(wb, inputBuffer);
			done = TRUE;
			break;
		    case 'c':
			done = TRUE;
			break;
		    }
		    break;
		case IDCMP_GADGETUP:
                    switch (gad->GadgetID) {
		    case inputField:
			// fallthrough
		    case executeButton:
                        // "Execute" button was pressed
                        // Get the text from the input field
			execute_command(wb, inputBuffer);
                        done = TRUE;
			break;
		    case cancelButton:
                        // "Cancel" button was pressed
                        done = TRUE;
			break;
                    }
		    break;
                }
            }
        }
    }

    // Save the input for next time.
    CopyMem(inputBuffer, wb->ExecuteBuffer, sizeof(wb->ExecuteBuffer));
    wb->ExecuteBuffer[sizeof(wb->ExecuteBuffer)-1] = 0;

exit:
    // Cleanup and close
    FreeGadgets(glist);
    CloseWindow(executeWindow);
    FreeVisualInfo(vi);
    UnlockPubScreen(NULL, screen);
}

static BOOL wbMenuPick(Class *cl, Object *obj, struct Window *win, UWORD menuNumber)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    Object *owin;
    struct MenuItem *item;
    BOOL quit = FALSE;

    owin = wbLookupWindow(cl, obj, win);

    D(bug("Menu: %x\n", menuNumber));
    while (menuNumber != MENUNULL) {
        BOOL handled = FALSE;

        item = ItemAddress(win->MenuStrip, menuNumber);

        /* Let the window have first opportunity */
        if (owin)
        {
            struct wbwm_MenuPick wbmpmsg;
            wbmpmsg.MethodID = WBWM_MENUPICK;
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
            case WBMENU_ID(WBMENU_WB_QUIT):
                quit = TRUE;
                break;
	    case WBMENU_ID(WBMENU_WB_ABOUT):
		wbAbout(cl, obj, win);
		break;
	    case WBMENU_ID(WBMENU_WB_EXECUTE):
		wbExecute(cl, obj, win);
		break;
            case WBMENU_ID(WBMENU_WB_SHUTDOWN):
                rc = EasyRequest(NULL, &es, NULL, TAG_DONE);
#ifdef __AROS__
                if (rc == 1) {
                    /* TODO: Ask if the user wants a shutdown or reboot */
                    ShutdownA(SD_ACTION_POWEROFF);
                } else {
                    /* Can't power off. Try to reboot. */
                    ShutdownA(SD_ACTION_COLDREBOOT);
                }
#endif
                ColdReboot();
                break;
            }
        }

        menuNumber = item->NextSelect;
    }

    return quit;
}

static void wbIntuiTick(Class *cl, Object *obj, struct Window *win)
{
    Object *owin;

    if ((owin = wbLookupWindow(cl, obj, win))) {
        STACKED ULONG wbintuitmethodID;
        wbintuitmethodID = WBWM_INTUITICK;
        DoMethodA(owin, (Msg)&wbintuitmethodID);
    }
}

static void wbHideAllWindows(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    while ((owin = NextObject(&ostate))) {
        STACKED ULONG wbhidemethodid;
        wbhidemethodid = WBWM_HIDE;
        DoMethodA(owin, (Msg)&wbhidemethodid);
    }
}

static void wbShowAllWindows(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    while ((owin = NextObject(&ostate))) {
        STACKED ULONG wbshowmethodid;
        wbshowmethodid = WBWM_SHOW;
        DoMethodA(owin, (Msg)&wbshowmethodid);
    }
}

static void wbCloseAllWindows(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    Object *ostate = (Object *)my->Windows.mlh_Head;
    Object *owin;

    while ((owin = NextObject(&ostate))) {
        struct opMember opmmsg;
        opmmsg.MethodID = OM_REMMEMBER;
        opmmsg.opam_Object = owin;
        DoMethodA(obj, (Msg)&opmmsg);
        DisposeObject(owin);
    }
}

// WBAM_WORKBENCH - Register and handle all workbench events
static IPTR WBAppWorkbench(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbApp *my = INST_DATA(cl, obj);
    BOOL done = FALSE;

    CurrentDir(BNULL);

    if (RegisterWorkbench(my->AppPort)) {
        while (!done) {
            ULONG mask;

            mask = Wait(my->AppMask | my->WinMask);

            if (mask & my->AppMask) {
                struct WBHandlerMessage *wbhm;
                wbhm = (APTR)GetMsg(my->AppPort);

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

            if (mask & my->WinMask) {
                struct IntuiMessage *im;

                im = GT_GetIMsg(my->WinPort);

                D(bug("im=%p, Class=%d, Code=%d\n", im, im->Class, im->Code));
                switch (im->Class) {
                case IDCMP_CLOSEWINDOW:
                    /* Dispose the window */
                    wbCloseWindow(cl, obj, im->IDCMPWindow);
                    break;
                case IDCMP_REFRESHWINDOW:
                    /* call WBWM_REFRESH on the window */
                    wbRefreshWindow(cl, obj, im->IDCMPWindow);
                    break;
                case IDCMP_NEWSIZE:
                    /* call WBWM_NEWSIZE on the window */
                    wbNewSizeWindow(cl, obj, im->IDCMPWindow);
                    break;
                case IDCMP_MENUPICK:
                    done = wbMenuPick(cl, obj, im->IDCMPWindow, im->Code);
                    break;
                case IDCMP_INTUITICKS:
                    wbIntuiTick(cl, obj, im->IDCMPWindow);
                    break;
                }

                GT_ReplyIMsg(im);
            }
        }

        wbCloseAllWindows(cl, obj);

        UnregisterWorkbench(my->AppPort);
    }

    return FALSE;
}

static IPTR dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    case OM_NEW:       rc = WBAppNew(cl, obj, (APTR)msg); break;
    case OM_DISPOSE:   rc = WBAppDispose(cl, obj, (APTR)msg); break;
    case OM_ADDMEMBER: rc = WBAppAddMember(cl, obj, (APTR)msg); break;
    case OM_REMMEMBER: rc = WBAppRemMember(cl, obj, (APTR)msg); break;
    case WBAM_WORKBENCH:rc = WBAppWorkbench(cl, obj, (APTR)msg); break;
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
        cl->cl_Dispatcher.h_SubEntry = dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
