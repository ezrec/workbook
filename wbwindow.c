/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Window Class
*/

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <intuition/icclass.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/gadtools.h>
#ifdef __AROS__
#include <proto/workbench.h>
#else
#include <proto/wb.h>
#endif
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/icon.h>

#include <intuition/classusr.h>
#include <libraries/gadtools.h>

#include "workbook_intern.h"
#include "workbook_menu.h"
#include "classes.h"

struct wbWindow_Icon {
    struct MinNode wbwiNode;
    Object *wbwiObject;
};

struct wbWindow {
    STRPTR         Path;
    BPTR           Lock;
    struct Window *Window;
    struct Menu   *Menu;
    Object        *ScrollH;
    Object        *ScrollV;
    Object        *Area;      /* Virual area of icons */
    Object        *Set;       /* Set of icons */

    ULONG          Flags;
    IPTR           Tick;

    /* Temporary path buffer */
    TEXT           PathBuffer[PATH_MAX];
    TEXT           ScreenTitle[256];

    /* List of icons in this window */
    struct MinList IconList;
};

#define WBWF_USERPORT   (1 << 0)    /* Window has a custom port */
#define WBWF_SHOW_ALL   (1 << 1)    /* Show all files */

#define Broken NM_ITEMDISABLED |

static const struct NewMenu WBWindow_menu[] =  {
    WBMENU_TITLE(WBMENU_WB),
        WBMENU_ITEM(WBMENU_WB_BACKDROP),
        WBMENU_ITEM(WBMENU_WB_EXECUTE),
        WBMENU_ITEM(WBMENU_WB_SHELL),
        WBMENU_ITEM(WBMENU_WB_ABOUT),
        WBMENU_BAR,
        WBMENU_ITEM(WBMENU_WB_QUIT),
        WBMENU_ITEM(WBMENU_WB_SHUTDOWN),
    WBMENU_TITLE(WBMENU_WN),
        WBMENU_ITEM(WBMENU_WN_NEW_DRAWER),
        WBMENU_ITEM(WBMENU_WN_OPEN_PARENT),
        WBMENU_ITEM(WBMENU_WN_UPDATE),
        WBMENU_ITEM(WBMENU_WN_SELECT_ALL),
        WBMENU_ITEM(WBMENU_WN_SELECT_NONE),
        WBMENU_SUBTITLE(WBMENU_WN__SNAP),
            WBMENU_SUBITEM(WBMENU_WN__SNAP_WINDOW),
            WBMENU_SUBITEM(WBMENU_WN__SNAP_ALL),
        WBMENU_SUBTITLE(WBMENU_WN__SHOW),
            WBMENU_SUBITEM(WBMENU_WN__SHOW_ICONS),
            WBMENU_SUBITEM(WBMENU_WN__SHOW_ALL),
        WBMENU_SUBTITLE(WBMENU_WN__VIEW),
            WBMENU_SUBITEM(WBMENU_WN__VIEW_ICON),
            WBMENU_SUBITEM(WBMENU_WN__VIEW_DETAILS),
    WBMENU_TITLE(WBMENU_IC),
        WBMENU_ITEM(WBMENU_IC_OPEN),
        WBMENU_ITEM(WBMENU_IC_COPY),
        WBMENU_ITEM(WBMENU_IC_RENAME),
        WBMENU_ITEM(WBMENU_IC_INFO),
        WBMENU_BAR,
        WBMENU_ITEM(WBMENU_IC_SNAPSHOT),
        WBMENU_ITEM(WBMENU_IC_UNSNAPSHOT),
        WBMENU_ITEM(WBMENU_IC_LEAVE_OUT),
        WBMENU_ITEM(WBMENU_IC_PUT_AWAY),
        WBMENU_BAR,
        WBMENU_ITEM(WBMENU_IC_DELETE),
        WBMENU_ITEM(WBMENU_IC_FORMAT),
        WBMENU_ITEM(WBMENU_IC_EMPTY_TRASH),
    { NM_END },
};

static BOOL wbMenuEnable(Class *cl, Object *obj, int id, BOOL onoff)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    int menu = -1, item = -1, sub = -1;
    UWORD MenuNumber = MENUNULL;
    BOOL rc = FALSE;

    for (const struct NewMenu *nm = WBWindow_menu; nm->nm_Type != NM_END; nm++) {
        switch (nm->nm_Type) {
        case NM_TITLE:
            menu++;
            item = -1;
            sub = -1;
            break;
        case IM_ITEM:
        case NM_ITEM:
            item++;
            sub = -1;
            break;
        case IM_SUB:
        case NM_SUB:
            sub++;
            break;
        }

        if (nm->nm_UserData == (APTR)(IPTR)id) {
            MenuNumber = FULLMENUNUM(menu, item, sub);
            break;
        }
    }

    if (MenuNumber != MENUNULL) {
        if (onoff)
            OnMenu(my->Window, MenuNumber);
        else
            OffMenu(my->Window, MenuNumber);

        rc = TRUE;
    }

    return rc;
}

static ULONG wbFilterIcons_Hook(struct Hook *hook, struct ExAllData *ead, LONG *type)
{
    int i;
    struct wbWindow *my = hook->h_Data;

    if (stricmp(ead->ed_Name, "disk.info") == 0) {
        D(bug("- %s (disk)\n", ead->ed_Name));
        return FALSE;
    }

    BOOL show_all = (my->Flags & WBWF_SHOW_ALL) != 0;

    i = strlen(ead->ed_Name);
    if (i >= 5 && stricmp(&ead->ed_Name[i-5], ".info") == 0) {
        if (show_all) {
            D(bug("- %s (icon)\n", ead->ed_Name));
            return FALSE;
        } else {
            ead->ed_Name[i-5] = 0;
            D(bug("+ %s (icon)\n", ead->ed_Name));
            return TRUE;
        }
    }

    if (stricmp(ead->ed_Name, ".backdrop") == 0) {
        D(bug("- %s (backdrop)\n", ead->ed_Name));
        return FALSE;
    }

    D(bug("%lc %s (default)\n", show_all ? '+' : '-', ead->ed_Name));
    return show_all;
}

static int wbwiIconCmp(Class *cl, Object *obj, Object *a, Object *b)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;

    CONST_STRPTR al = NULL, bl = NULL;

    GetAttr(WBIA_Label, a, (IPTR *)&al);
    GetAttr(WBIA_Label, b, (IPTR *)&bl);

    if (al == bl)
        return 0;

    if (al == NULL)
        return 1;

    if (bl == NULL)
        return -1;

    return Stricmp(al, bl);
}

static void wbwiAppend(Class *cl, Object *obj, Object *iobj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;

    wbwi = AllocMem(sizeof(*wbwi), MEMF_ANY);
    if (!wbwi) {
        DisposeObject(iobj);
    } else {
        struct wbWindow_Icon *tmp, *pred = NULL;
        wbwi->wbwiObject = iobj;

        /* Insert in Alpha order */
        ForeachNode(&my->IconList, tmp) {
            if (wbwiIconCmp(cl, obj, tmp->wbwiObject, wbwi->wbwiObject) == 0) {
                D(bug("%s: Duplicated icon in '%s'\n", my->Path));
                DisposeObject(iobj);
                return;
            }
            if (wbwiIconCmp(cl, obj, tmp->wbwiObject, wbwi->wbwiObject) < 0)
                break;
            pred = tmp;
        }

        Insert((struct List *)&my->IconList, (struct Node *)wbwi, (struct Node *)pred);
    }
}

static void wbAddFiles(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct ExAllControl *eac;
    struct ExAllData *ead;
    const ULONG eadSize = sizeof(struct ExAllData) + 1024;
    TEXT *path;
    int file_part;

    D(bug("%s: Add files...\n", __func__));

    path = AllocVec(1024, MEMF_ANY);
    if (!path) {
        D(bug("%s: Unable to allocate path.\n", __func__));
        return;
    }

    if (!NameFromLock(my->Lock, path, 1024)) {
        D(bug("%s: Unable to get path from lock.\n", __func__));
        FreeVec(path);
        return;
    }
    file_part = strlen(path);

    ead = AllocVec(eadSize, MEMF_CLEAR);
    if (ead != NULL) {
        eac = AllocDosObject(DOS_EXALLCONTROL, NULL);
        if (eac != NULL) {
            struct Hook hook;
            LONG more = TRUE;

            hook.h_Entry = HookEntry;
            hook.h_SubEntry = wbFilterIcons_Hook;
            hook.h_Data = my;

            eac->eac_MatchFunc = &hook;
            while (more) {
                struct ExAllData *tmp = ead;
                int i;

                more = ExAll(my->Lock, ead, eadSize, ED_NAME, eac);
                D(bug("%s: more=%ld\n", __func__, more));
                for (i = 0; i < eac->eac_Entries; i++, tmp=tmp->ed_Next) {
                    Object *iobj;
                    path[file_part] = 0;
                    if (AddPart(path, tmp->ed_Name, 1024)) {
                        iobj = NewObject(WBIcon, NULL,
                                WBIA_File, path,
                                WBIA_Label, tmp->ed_Name,
                                WBIA_Screen, my->Window->WScreen,
                                TAG_END);
                        if (iobj != NULL) {
                            wbwiAppend(cl, obj, iobj);
                        }
                    } else {
                        D(bug("%s: Out of space for name '%s'\n", tmp->ed_Name));
                    }
                }
            }
            FreeDosObject(DOS_EXALLCONTROL, eac);
        }
        FreeVec(ead);
    }

    FreeVec(path);

    D(bug("%s: Added!\n", __func__));
}

static void wbAddVolumeIcons(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct DosList *dl;
    char text[FILENAME_MAX];

    /* Add all the DOS disks */
    dl = LockDosList(LDF_VOLUMES | LDF_READ);

    if (dl != BNULL) {
        struct DosList *tdl;

        tdl = dl;
        while ((tdl = NextDosEntry(tdl, LDF_VOLUMES))) {
            Object *iobj;
           
            CopyMem(AROS_BSTR_ADDR(tdl->dol_Name), text, AROS_BSTR_strlen(tdl->dol_Name));
            CopyMem(":",&text[AROS_BSTR_strlen(tdl->dol_Name)],2);
            
            iobj = NewObject(WBIcon, NULL,
                    WBIA_File, text,
                    WBIA_Label, AROS_BSTR_ADDR(tdl->dol_Name),
                    WBIA_Screen, my->Window->WScreen,
                    TAG_END);
            D(bug("Volume: %s => %p\n", text, iobj));
            if (iobj)
                wbwiAppend(cl, obj, iobj);
        }
        UnLockDosList(LDF_VOLUMES | LDF_READ);
    }
}

static void wbAddAppIcons(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct DiskObject *icon;
    char text[FILENAME_MAX];

    /* Add all the AppIcons */
    icon = NULL;
    while ((icon = GetNextAppIcon(icon, &text[0]))) {
        Object *iobj = NewObject(WBIcon, NULL,
                              WBIA_Icon, icon,
                              WBIA_Screen, my->Window->WScreen,
                              TAG_END);
        if (iobj != NULL)
            wbwiAppend(cl, obj, iobj);
    }
}

static void wbRedimension(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct Window *win = my->Window;
    struct IBox    real;     /* pos & size of the inner window area */
    IPTR setWidth = 0, setHeight = 0;

    real.Left = win->BorderLeft;
    real.Top  = win->BorderTop;
    real.Width = win->Width - (win->BorderLeft + win->BorderRight);
    real.Height= win->Height- (win->BorderTop  + win->BorderBottom);

    D(bug("%s: Real   (%d,%d) %dx%d\n", __func__,
                real.Left, real.Top, real.Width, real.Height));
    D(bug("%s: Border (%d,%d) %dx%d\n", __func__,
                my->Window->BorderLeft, my->Window->BorderTop,
                my->Window->BorderRight, my->Window->BorderBottom));

    SetAttrs(my->Area, GA_Top, real.Top,
                       GA_Left,  real.Left,
                       GA_Width, real.Width,
                       GA_Height, real.Height,
                       TAG_END);

    SetAttrs(my->ScrollH, PGA_Visible, real.Width,
                          GA_Left, real.Left,
                          GA_RelBottom, -(my->Window->BorderBottom - 2),
                          GA_Width, real.Width,
                          GA_Height, my->Window->BorderBottom - 3,
                          TAG_END);

    SetAttrs(my->ScrollV, PGA_Visible, real.Height,
                          GA_RelRight, -(my->Window->BorderRight - 2),
                          GA_Top, real.Top,
                          GA_Width, my->Window->BorderRight - 3,
                          GA_Height, real.Height,
                          TAG_END);

    GetAttr(GA_Width, my->Set, &setWidth);
    GetAttr(GA_Height, my->Set, &setHeight);
    UpdateAttrs(obj, NULL, 0,
                     WBVA_VirtWidth, setWidth,
                     WBVA_VirtHeight, setHeight,
                     TAG_END);

    /* Clear the background to the right of the icons*/
    if (setWidth < real.Width) {
        SetAPen(win->RPort,0);
        RectFill(win->RPort, win->BorderLeft + setWidth, win->BorderTop,
                win->Width - win->BorderRight - 1,
                win->Height - win->BorderBottom - 1);
    } else {
        setWidth = real.Width;
    }

    /* Clear the background beneath the icons*/
    if (setHeight < real.Height) {
        SetAPen(win->RPort,0);
        RectFill(win->RPort, win->BorderLeft, win->BorderTop + setHeight,
                setWidth - win->BorderRight - 1,
                win->Height - win->BorderBottom - 1);
    }

}

/* Rescan the Lock for new entries */
static void wbRescan(Class *cl, Object *obj)
{
    struct opMember opmmsg;
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;

    /* We're going to busy for a while */
    D(bug("BUSY....\n"));
    SetWindowPointer(my->Window, WA_BusyPointer, TRUE, TAG_END);

    /* Remove and undisplay any existing icons */
    opmmsg.MethodID = OM_REMMEMBER;
    while ((wbwi = (struct wbWindow_Icon *)REMHEAD((struct List *)&my->IconList)) != NULL) {
        opmmsg.opam_Object = wbwi->wbwiObject;
        DoMethodA(my->Set, (Msg)&opmmsg);
        DisposeObject(wbwi->wbwiObject);
        FreeMem(wbwi, sizeof(*wbwi));
    }

    /* Scan for new icons, and add them to the list
     */
    if (my->Lock == BNULL) {
        /* Root window */
        wbAddVolumeIcons(cl, obj);
        wbAddAppIcons(cl, obj);
    } else {
        /* Directory window */
        wbAddFiles(cl, obj);
    }

    /* Display the new icons */
    opmmsg.MethodID = OM_ADDMEMBER;
    ForeachNode(&my->IconList, wbwi)
    {
        opmmsg.opam_Object = wbwi->wbwiObject;
        DoMethodA(my->Set, (Msg)&opmmsg);
    }

    /* Adjust the scrolling regions */
    wbRedimension(cl, obj);

    /* Return the point back to normal */
    SetWindowPointer(my->Window, WA_BusyPointer, FALSE, TAG_END);
    D(bug("Not BUSY....\n"));
}


static const struct TagItem scrollv2window[] = {
        { PGA_Top, WBVA_VirtTop },
        { TAG_END, 0 },
};

static const struct TagItem scrollh2window[] = {
        { PGA_Top, WBVA_VirtLeft },
        { TAG_END, 0 },
};

static void wbFixBorders(struct Window *win)
{
    int bb, br;

    bb = 16 - win->BorderBottom;
    br = 16 - win->BorderRight;

    win->BorderBottom += bb;
    win->BorderRight += br;
}

static IPTR WBWindowNew(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my;
    struct MsgPort *userport;
    CONST_STRPTR path;
    ULONG idcmp;
    IPTR rc = 0;
    APTR vis;
    struct wbWindow_Icon *wbwi;

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return rc;

    obj = (Object *)rc;
    my = INST_DATA(cl, obj);

    NEWLIST(&my->IconList);
    my->Flags = 0;

    path = (CONST_STRPTR)GetTagData(WBWA_Path, (IPTR)NULL, ops->ops_AttrList);
    if (path == NULL) {
        my->Lock = BNULL;
        my->Path = NULL;
    } else {
        my->Lock = Lock(path, SHARED_LOCK);
        if (my->Lock == BNULL)
            goto error;

        my->Path = AllocVec(strlen(path)+1, MEMF_ANY);
        if (my->Path == NULL)
            goto error;

        strcpy(my->Path, path);
    }

    /* Create icon set */
    my->Set = NewObject(WBSet, NULL,
                TAG_END);

    idcmp = IDCMP_MENUPICK | IDCMP_INTUITICKS;
    if (my->Path == NULL) {
        my->Window = OpenWindowTags(NULL,
                        WA_IDCMP, 0,
                        WA_Backdrop,    TRUE,
                        WA_WBenchWindow, TRUE,
                        WA_Borderless,  TRUE,
                        WA_Activate,    TRUE,
                        WA_SmartRefresh, TRUE,
                        WA_NewLookMenus, TRUE,
                        WA_PubScreen, NULL,
                        ops->ops_AttrList == NULL ? TAG_END : TAG_MORE, ops->ops_AttrList );
        my->Window->BorderTop = my->Window->WScreen->BarHeight+1;
    } else {
        struct DiskObject *icon;
        struct NewWindow *nwin = NULL;
        struct TagItem extra[] = {
            { WA_Left, 64 },
            { WA_Top, 64 },
            { WA_Width, 200, },
            { WA_Height, 150, },
            { ops->ops_AttrList == NULL ? TAG_END : TAG_MORE, (IPTR)ops->ops_AttrList },
        };

        icon = GetDiskObjectNew(my->Path);
        if (icon == NULL)
            goto error;

        if (icon->do_DrawerData) {
            // If we have dd_NewWindow data, don't override the window placement via extra[]
            nwin = &icon->do_DrawerData->dd_NewWindow;
            D(bug("%s: NewWindow %p\n", __func__, nwin));
            extra[0].ti_Tag = ops->ops_AttrList == NULL ? TAG_END : TAG_MORE;
            extra[0].ti_Data = (IPTR)ops->ops_AttrList;
        }

        idcmp |= IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW;
        my->Window = OpenWindowTags(nwin,
                        WA_IDCMP, 0,
                        WA_MinWidth, 100,
                        WA_MinHeight, 100,
                        WA_MaxWidth, ~0,
                        WA_MaxHeight, ~0,
                        WA_Backdrop, FALSE,
                        WA_WBenchWindow, TRUE,
                        WA_Title,    my->Path,
                        WA_SmartRefresh, TRUE,
                        WA_SizeGadget, TRUE,
                        WA_DragBar, TRUE,
                        WA_DepthGadget, TRUE,
                        WA_CloseGadget, TRUE,
                        WA_Activate, TRUE,
                        WA_NewLookMenus, TRUE,
                        WA_AutoAdjust, TRUE,
                        WA_PubScreen, NULL,
                        TAG_MORE, (IPTR)&extra[0] );

        if (my->Window)
            wbFixBorders(my->Window);

        FreeDiskObject(icon);
    }

    if (!my->Window)
        goto error;

    /* If we want a shared port, do it. */
    userport = (struct MsgPort *)GetTagData(WBWA_UserPort, (IPTR)NULL, ops->ops_AttrList);
    if (userport) {
        my->Flags |= WBWF_USERPORT;
        my->Window->UserPort = userport;
    }
    ModifyIDCMP(my->Window, idcmp);

    /* The gadgets' layout will be performed during wbRedimension
     */
    AddGadget(my->Window, (struct Gadget *)(my->Area = NewObject(WBVirtual, NULL,
                WBVA_Gadget, (IPTR)my->Set,
                TAG_END)), 0);

    /* Add the verical scrollbar */
    AddGadget(my->Window, (struct Gadget *)(my->ScrollV = NewObject(NULL, "propgclass",
                GA_RightBorder, TRUE,

                ICA_TARGET, (IPTR)obj,
                ICA_MAP, (IPTR)scrollv2window,
                PGA_Freedom, FREEVERT,
                PGA_NewLook, TRUE,
                PGA_Borderless, TRUE,
                PGA_Total, 1,
                PGA_Visible, 1,
                PGA_Top, 0,
                TAG_END)), 0);

    /* Add the horizontal scrollbar */
    AddGadget(my->Window, (struct Gadget *)(my->ScrollH = NewObject(NULL, "propgclass",
                ICA_TARGET, (IPTR)obj,
                ICA_MAP, (IPTR)scrollh2window,
                PGA_Freedom, FREEHORIZ,
                PGA_NewLook, TRUE,
                PGA_Borderless, TRUE,
                PGA_Total, 1,
                PGA_Visible, 1,
                PGA_Top, 0,
                TAG_END)), 0);

    /* Send first intuitick */
    DoMethod(obj, WBWM_INTUITICK);

    my->Menu = CreateMenusA((struct NewMenu *)WBWindow_menu, NULL);
    if (my->Menu == NULL) {
        D(bug("%s: Unable to create  menus\n", __func__));
        goto error;
    }

    vis = GetVisualInfo(my->Window->WScreen, TAG_END);
    LayoutMenus(my->Menu, vis, TAG_END);
    FreeVisualInfo(vis);

    SetMenuStrip(my->Window, my->Menu);

    /* Disable opening the parent for root window
     * and disk paths.
     */
    if (my->Lock == BNULL) {
        wbMenuEnable(cl, obj, WBMENU_ID(WBMENU_WN_OPEN_PARENT), FALSE);
    } else {
        BPTR lock = ParentDir(my->Lock);
        if (lock == BNULL) {
            wbMenuEnable(cl, obj, WBMENU_ID(WBMENU_WN_OPEN_PARENT), FALSE);
        } else {
            UnLock(lock);
        }
    }

    SetAttrs(my->Set, WBSA_MaxWidth, my->Window->Width - (my->Window->BorderLeft + my->Window->BorderRight));
    RefreshGadgets(my->Window->FirstGadget, my->Window, NULL);

    wbRescan(cl, obj);

    return rc;

error:
    while ((wbwi = (APTR)GetHead((struct List *)&my->IconList)) != NULL) {
        Remove((struct Node *)wbwi);
        FreeMem(wbwi, sizeof(*wbwi));
    }

    if (my->Set)
        DisposeObject(my->Set);

    if (my->Window)
        CloseWindow(my->Window);

    if (my->Path)
        FreeVec(my->Path);

    if (my->Lock != BNULL)
        UnLock(my->Lock);

    DoSuperMethod(cl, obj, OM_DISPOSE, 0);
    return 0;
}

static IPTR WBWindowDispose(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;

    ClearMenuStrip(my->Window);
    FreeMenus(my->Menu);

    /* If we have a custom user port, be paranoid.
     * See the Autodocs for CloseWindow().
     */
    if (my->Flags & WBWF_USERPORT) {
        struct IntuiMessage *msg;
        struct Node *succ;

        Forbid();
        msg = (APTR)my->Window->UserPort->mp_MsgList.lh_Head;
        while ((succ = msg->ExecMessage.mn_Node.ln_Succ ) != NULL) {
            if (msg->IDCMPWindow == my->Window) {
                Remove((APTR)msg);
                ReplyMsg((struct Message *)msg);
            }

            msg = (struct IntuiMessage *) succ;
        }

        my->Window->UserPort = NULL;
        ModifyIDCMP(my->Window, 0);

        Permit();
    }

    /* We won't need our list of icons anymore */
    while ((wbwi = (APTR)GetHead((struct List *)&my->IconList)) != NULL) {
        Remove((struct Node *)wbwi);
        FreeMem(wbwi, sizeof(*wbwi));
    }

    /* As a side effect, this will close all the
     * gadgets attached to it.
     */
    CloseWindow(my->Window);

    /* .. except for my->Set */
    DisposeObject(my->Set);

    if (my->Path)
        FreeVec(my->Path);

    if (my->Lock != BNULL)
        UnLock(my->Lock);

    return DoSuperMethodA(cl, obj, msg);
}

// OM_GET
static IPTR WBWindowGet(Class *cl, Object *obj, struct opGet *opg)
{
    struct wbWindow *my = INST_DATA(cl, obj);
    IPTR rc = TRUE;

    switch (opg->opg_AttrID) {
    case WBWA_Path:
        *(opg->opg_Storage) = (IPTR)my->Path;
        break;
    case WBWA_Window:
        *(opg->opg_Storage) = (IPTR)my->Window;
        break;
    default:
        rc = DoSuperMethodA(cl, obj, (Msg)opg);
        break;
    }

    return rc;
}

// OM_UPDATE
static IPTR WBWindowUpdate(Class *cl, Object *obj, struct opUpdate *opu)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct TagItem *tstate;
    struct TagItem *tag;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)opu);

    /* Also send these to the Area */
    rc |= DoMethodA(my->Area, (Msg)opu);

    /* Update scrollbars if needed */
    tstate = opu->opu_AttrList;
    while ((tag = NextTagItem(&tstate))) {
        switch (tag->ti_Tag) {
        case WBVA_VirtLeft:
            rc = TRUE;
            break;
        case WBVA_VirtTop:
            rc = TRUE;
            break;
        case WBVA_VirtWidth:
            SetAttrs(my->ScrollH, PGA_Total, tag->ti_Data, TAG_END);
            rc = TRUE;
            break;
        case WBVA_VirtHeight:
            SetAttrs(my->ScrollV, PGA_Total, tag->ti_Data, TAG_END);
            rc = TRUE;
            break;
        }
    }

    if (rc && !(opu->opu_Flags & OPUF_INTERIM))
        RefreshGadgets(my->Window->FirstGadget, my->Window, NULL);

    return rc;
}

// WBWM_NEWSIZE
static IPTR WBWindowNewSize(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct Window *win = my->Window;
    struct Region *clip;

    SetAttrs(my->Set, WBSA_MaxWidth, win->Width - (win->BorderLeft + win->BorderRight));

    /* Clip to the window for drawing */
    clip = wbClipWindow(wb, win);
    wbRedimension(cl, obj);
    wbUnclipWindow(wb, win, clip);

    return 0;
}

static IPTR WBWindowRefresh(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct Window *win = my->Window;

    BeginRefresh(win);
    EndRefresh(win, TRUE);

    return 0;
}

static void NewCLI(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);

    BPTR dir;

    SetWindowPointer(my->Window, WA_BusyPointer, TRUE, TAG_END);
    dir = CurrentDir(my->Lock);
    Execute("", BNULL, BNULL);
    CurrentDir(dir);
    SetWindowPointer(my->Window, WA_BusyPointer, FALSE, TAG_END);
}

static IPTR WBWindowForSelectedIcons(Class *cl, Object *obj, IPTR MethodID)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;
    IPTR rc = 0;

    ForeachNode(&my->IconList, wbwi) {
        IPTR selected = FALSE;

        GetAttr(GA_Selected, wbwi->wbwiObject, &selected);
        if (selected)
            rc |= DoMethodA(wbwi->wbwiObject, (Msg)&MethodID);
    }

    return rc;
}

// WBWM_MENUPICK
static IPTR WBWindowMenuPick(Class *cl, Object *obj, struct wbwm_MenuPick *wbwmp)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct MenuItem *item = wbwmp->wbwmp_MenuItem;
    BPTR lock;
    IPTR rc = 0;

    switch (WBMENU_ITEM_ID(item)) {
    case WBMENU_ID(WBMENU_WN_OPEN_PARENT):
        if (my->Lock != BNULL) {
            lock = ParentDir(my->Lock);
            if (NameFromLock(lock, my->PathBuffer, sizeof(my->PathBuffer))) {
                OpenWorkbenchObject(my->PathBuffer, TAG_END);
            }
            UnLock(lock);
        }
        break;
    case WBMENU_ID(WBMENU_WN_UPDATE):
        rc = WBIM_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WN__SHOW_ICONS):
        my->Flags &= ~WBWF_SHOW_ALL;
        rc = WBIM_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WN__SHOW_ALL):
        my->Flags |= WBWF_SHOW_ALL;
        rc = WBIM_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WB_SHELL):
        NewCLI(cl, obj);
        break;
    case WBMENU_ID(WBMENU_IC_OPEN):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Open);
        break;
    case WBMENU_ID(WBMENU_IC_COPY):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Copy);
        break;
    case WBMENU_ID(WBMENU_IC_RENAME):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Rename);
        break;
    case WBMENU_ID(WBMENU_IC_INFO):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Info);
        break;
    case WBMENU_ID(WBMENU_IC_SNAPSHOT):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Snapshot);
        break;
    case WBMENU_ID(WBMENU_IC_UNSNAPSHOT):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Unsnapshot);
        break;
    case WBMENU_ID(WBMENU_IC_LEAVE_OUT):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Leave_Out);
        break;
    case WBMENU_ID(WBMENU_IC_PUT_AWAY):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Put_Away);
        break;
    case WBMENU_ID(WBMENU_IC_DELETE):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Delete);
        break;
    case WBMENU_ID(WBMENU_IC_FORMAT):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Format);
        break;
    case WBMENU_ID(WBMENU_IC_EMPTY_TRASH):
        rc = WBWindowForSelectedIcons(cl, obj, WBIM_Empty_Trash);
        break;
    default:
        rc = 0;
        break;
    }

    if (rc & WBIM_REFRESH) {
        wbRescan(cl, obj);
    }

    return rc;
}

// WBWM_INTUITICK
static IPTR WBWindowIntuiTick(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    IPTR rc = FALSE;

    if (my->Tick == 0) {
        IPTR val[6];

        val[0] = (IPTR)AS_STRING(WB_NAME);
        val[1] = WB_VERSION;
        val[2] = WB_REVISION;
        val[3] = AvailMem(MEMF_CHIP) / 1024;
        val[4] = AvailMem(MEMF_FAST) / 1024;
        val[5] = AvailMem(MEMF_ANY) / 1024;

        /* Update the window's title */
        RawDoFmt("%s %ld.%ld  Chip: %ldk, Fast: %ldk, Any: %ldk", (RAWARG)val,
                 RAWFMTFUNC_STRING, my->ScreenTitle);

        SetWindowTitles(my->Window, (CONST_STRPTR)-1, my->ScreenTitle);
        rc = TRUE;
    }

    /* Approx 10 IntuiTicks per second */
    my->Tick = (my->Tick + 1) % 10;

    return rc;
}

static IPTR dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    case OM_NEW:         rc = WBWindowNew(cl, obj, (APTR)msg); break;
    case OM_DISPOSE:     rc = WBWindowDispose(cl, obj, (APTR)msg); break;
    case OM_GET:         rc = WBWindowGet(cl, obj, (APTR)msg); break;
    case OM_UPDATE:      rc = WBWindowUpdate(cl, obj, (APTR)msg); break;
    case WBWM_NEWSIZE:   rc = WBWindowNewSize(cl, obj, (APTR)msg); break;
    case WBWM_MENUPICK:  rc = WBWindowMenuPick(cl, obj, (APTR)msg); break;
    case WBWM_INTUITICK: rc = WBWindowIntuiTick(cl, obj, (APTR)msg); break;
    case WBWM_REFRESH:   rc = WBWindowRefresh(cl, obj, (APTR)msg); break;
    default:             rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBWindow_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "rootclass", NULL,
                    sizeof(struct wbWindow),
                    0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
