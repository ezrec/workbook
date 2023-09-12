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

#include <dos/dostags.h>
#include <intuition/classusr.h>
#include <libraries/gadtools.h>

#include "workbook_intern.h"
#include "workbook_menu.h"
#include "classes.h"
#include "wbcurrent.h"

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

    ULONG          dd_Flags;
    UWORD          dd_ViewModes;

    /* Temporary path buffer */
    TEXT           ScreenTitle[256];

    ULONG          AvailChip;
    ULONG          AvailFast;
    ULONG          AvailAny;

    /* List of icons in this window */
    struct MinList IconList;

    // Notify request for this drawer.
    struct {
        struct NotifyRequest Request;
        struct MsgPort *NotifyPort;
        BOOL Cached;                    // Contents have been cached.
    } Notify;
};

#define Broken NM_ITEMDISABLED |

static const struct NewMenu WBWindow_menu[] =  {
    WBMENU_TITLE(WBMENU_WB),
        WBMENU_ITEM(WBMENU_WB_BACKDROP),
        WBMENU_ITEM(WBMENU_WB_EXECUTE),
        WBMENU_ITEM(WBMENU_WB_SHELL),
        WBMENU_ITEM(WBMENU_WB_ABOUT),
        WBMENU_BAR,
        WBMENU_ITEM(WBMENU_WB_CUST_UPDATER),
        WBMENU_ITEM(WBMENU_WB_CUST_AMISTORE),
        WBMENU_BAR,
        WBMENU_ITEM(WBMENU_WB_QUIT),
        WBMENU_ITEM(WBMENU_WB_SHUTDOWN),
    WBMENU_TITLE(WBMENU_WN),
        WBMENU_ITEM(WBMENU_WN_NEW_DRAWER),
        WBMENU_ITEM(WBMENU_WN_OPEN_PARENT),
        WBMENU_ITEM(WBMENU_WN_UPDATE),
        WBMENU_ITEM(WBMENU_WN_SELECT_CONTENTS),
        WBMENU_ITEM(WBMENU_WN_CLEAN_UP),
        WBMENU_BAR,
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

static ULONG wbMenuNumber(int id)
{
    int menu = -1, item = -1, sub = -1;
    ULONG menu_number = MENUNULL;

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
            menu_number = FULLMENUNUM(menu, item, sub);
            break;
        }
    }

    return menu_number;
}

static BOOL wbFilterFileInfoBlock(struct wbWindow *my, struct FileInfoBlock *fib)
{
    int i;

    D(bug("%s ", fib->fib_FileName));

    if (stricmp(fib->fib_FileName, "disk.info") == 0) {
        D(bug("- (disk)\n"));
        return FALSE;
    }

    BOOL show_all = (my->dd_Flags & DDFLAGS_SHOWALL) != 0;

    i = strlen(fib->fib_FileName);
    if (i >= 5 && stricmp(&fib->fib_FileName[i-5], ".info") == 0) {
        if (show_all) {
            D(bug("- (icon)\n"));
            return FALSE;
        } else {
            fib->fib_FileName[i-5] = 0;
            D(bug("+ (icon)\n"));
            return TRUE;
        }
    }

    if (stricmp(fib->fib_FileName, ".backdrop") == 0) {
        D(bug("- (backdrop)\n"));
        return FALSE;
    }

    D(bug("%lc (default)\n", show_all ? '+' : '-'));
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
                D(bug("%s: Duplicated icon in '%s'\n", __func__, my->Path));
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

    D(bug("%s: Add files...\n", __func__));

    struct FileInfoBlock *fib = AllocDosObject(DOS_FIB, NULL);
    if (fib != NULL) {
        D(bug("%s: Examine %ld\n", __func__, (IPTR)BADDR(my->Lock)));
        if (!Examine(my->Lock, fib)) {
            wbPopupIoErr(wb, "Update", IoErr(), my->Path);
        } else {
            while (ExNext(my->Lock, fib)) {
                if (wbFilterFileInfoBlock(my, fib)) {
                    Object *iobj = NewObject(WBIcon, NULL,
                            WBIA_ParentLock, my->Lock,
                            WBIA_File, fib->fib_FileName,
                            WBIA_Screen, my->Window->WScreen,
                            TAG_END);
                    if (iobj != NULL) {
                        wbwiAppend(cl, obj, iobj);
                    }
                }
            }
            LONG ioerr = IoErr();
            if (ioerr != ERROR_NO_MORE_ENTRIES) {
                wbPopupIoErr(wb, "Update", ioerr, my->Path);
            }
        }
    }
    FreeDosObject(DOS_FIB, fib);

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
                    WBIA_ParentLock, BNULL,
                    WBIA_Screen, my->Window->WScreen,
                    TAG_END);
            D(bug("Volume: %s => %p\n", text, iobj));
            if (iobj) {
                wbwiAppend(cl, obj, iobj);
            }
        }
        UnLockDosList(LDF_VOLUMES | LDF_READ);
    }
}

static void wbWindowRedimension(Class *cl, Object *obj)
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

    D(bug("%s: Real   (%ld,%ld) %ldx%ld\n", __func__,
                (IPTR)real.Left, (IPTR)real.Top, (IPTR)real.Width, (IPTR)real.Height));
    D(bug("%s: Border (%ld,%ld) %ldx%ld\n", __func__,
                (IPTR)my->Window->BorderLeft, (IPTR)my->Window->BorderTop,
                (IPTR)my->Window->BorderRight, (IPTR)my->Window->BorderBottom));

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

// Invalidate the cache.
static IPTR WBWindow__WBWM_InvalidateContents(Class *cl, Object *obj, Msg msg)
{
    struct wbWindow *my = INST_DATA(cl, obj);

    my->Notify.Cached = FALSE;

    return 0;
}

/* Rescan the Lock for new entries */
static IPTR WBWindow__WBWM_CacheContents(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;

    if (my->Notify.Cached) {
        return 0;
    }

    /* We're going to busy for a while */
    D(bug("%s: BUSY....\n", __func__));
    SetWindowPointer(my->Window, WA_BusyPointer, TRUE, TAG_END);

    /* Remove and undisplay any existing icons */
    struct wbWindow_Icon *tmp;
    ForeachNodeSafe(&my->IconList, wbwi, tmp) {
        DoMethod(my->Set, OM_REMMEMBER, wbwi->wbwiObject);
        DisposeObject(wbwi->wbwiObject);
        RemoveMinNode(&wbwi->wbwiNode);
        FreeMem(wbwi, sizeof(*wbwi));
    }

    /* Scan for new icons, and add them to the list
     */
    if (my->Lock == BNULL) {
        /* Root window */
        wbAddVolumeIcons(cl, obj);
    } else {
        /* Directory window */
        wbAddFiles(cl, obj);
    }

    /* Add the new icons */
    ForeachNode(&my->IconList, wbwi)
    {
        DoMethod(my->Set, OM_ADDMEMBER, (IPTR)wbwi->wbwiObject);
    }

    /* Arrange the set */
    DoGadgetMethod((struct Gadget *)my->Set, my->Window, NULL, (IPTR)WBSM_Clean_Up, NULL);

    /* Adjust the scrolling regions */
    wbWindowRedimension(cl, obj);

    /* Return the point back to normal */
    SetWindowPointer(my->Window, WA_BusyPointer, FALSE, TAG_END);
    D(bug("%s: Not BUSY....\n", __func__));

    my->Notify.Cached = TRUE;

    return 0;
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

// OM_NEW
static IPTR WBWindow__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my;
    CONST_STRPTR path;
    ULONG idcmp;
    IPTR rc = 0;
    APTR vis;

    struct Screen *screen = (struct Screen *)GetTagData(WBWA_Screen, (IPTR)NULL, ops->ops_AttrList);
    if (screen == NULL) {
        return 0;
    }

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return rc;

    obj = (Object *)rc;
    my = INST_DATA(cl, obj);

    my->dd_Flags = DDFLAGS_SHOWDEFAULT;
    my->dd_ViewModes = DDVM_BYDEFAULT;

    NEWLIST(&my->IconList);

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
    struct MsgPort *userport = (struct MsgPort *)GetTagData(WBWA_UserPort, (IPTR)NULL, ops->ops_AttrList);
    struct MsgPort *notifyport = (struct MsgPort *)GetTagData(WBWA_NotifyPort, (IPTR)NULL, ops->ops_AttrList);

    if (my->Path == NULL) {
        my->Window = OpenWindowTags(NULL,
                        WA_IDCMP, userport ? 0 : idcmp,
                        WA_Backdrop,    TRUE,
                        WA_WBenchWindow, TRUE,
                        WA_Borderless,  TRUE,
                        WA_Activate,    TRUE,
                        WA_NewLookMenus, TRUE,
                        WA_PubScreen, screen,
                        WA_BusyPointer, TRUE,
                        ops->ops_AttrList == NULL ? TAG_END : TAG_MORE, ops->ops_AttrList );
        my->Window->BorderTop = my->Window->WScreen->BarHeight+1;
    } else {
        struct DiskObject *icon;
        struct NewWindow *nwin = NULL;
        struct TagItem extra[5] = {
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
            // If we have dd_NewWindow data, override the window placement via extra[]
            nwin = &icon->do_DrawerData->dd_NewWindow;
            if (nwin->Width > 32 && nwin->Height > 32) {
                D(bug("%s: NewWindow %p\n", __func__, nwin));
                extra[0].ti_Tag = ops->ops_AttrList == NULL ? TAG_END : TAG_MORE;
                extra[0].ti_Data = (IPTR)ops->ops_AttrList;
                extra[1].ti_Tag = TAG_IGNORE;
                extra[2].ti_Tag = TAG_IGNORE;
                extra[3].ti_Tag = TAG_IGNORE;
            }
        }

        idcmp |= IDCMP_NEWSIZE | IDCMP_CLOSEWINDOW;
        my->Window = OpenWindowTags(nwin,
                        WA_IDCMP, userport ? 0 : idcmp,
                        WA_MinWidth, 100,
                        WA_MinHeight, 100,
                        WA_MaxWidth, ~0,
                        WA_MaxHeight, ~0,
                        WA_Backdrop, FALSE,
                        WA_WBenchWindow, TRUE,
                        WA_Title,    my->Path,
                        WA_SizeGadget, TRUE,
                        WA_DragBar, TRUE,
                        WA_DepthGadget, TRUE,
                        WA_CloseGadget, TRUE,
                        WA_Activate, TRUE,
                        WA_NewLookMenus, TRUE,
                        WA_SmartRefresh, TRUE,
                        WA_AutoAdjust, TRUE,
                        WA_PubScreen, NULL,
                        WA_BusyPointer, TRUE,
                        TAG_MORE, (IPTR)&extra[0] );

        if (my->Window)
            wbFixBorders(my->Window);

        if (icon->do_DrawerData) {
            my->dd_Flags = icon->do_DrawerData->dd_Flags;
            my->dd_ViewModes = icon->do_DrawerData->dd_ViewModes;
        }

        FreeDiskObject(icon);
    }

    if (!my->Window)
        goto error;

    /* If we want a shared port, do it. */
    if (userport && idcmp) {
        my->Window->UserPort = userport;
        ModifyIDCMP(my->Window, idcmp);
    }

    /* The gadgets' layout will be performed during wbWindowRedimension
     */
    AddGadget(my->Window, (struct Gadget *)(my->Area = NewObject(WBVirtual, NULL,
                WBVA_Gadget, (IPTR)my->Set,
                GA_Left, my->Window->BorderLeft,
                GA_Top, my->Window->BorderTop,
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

    my->Menu = CreateMenusA((struct NewMenu *)WBWindow_menu, NULL);
    if (my->Menu == NULL) {
        D(bug("%s: Unable to create  menus\n", __func__));
        goto error;
    }

    // Get some useful menu numbers.
    ULONG mn_show_icons = wbMenuNumber(WBMENU_ID(WBMENU_WN__SHOW_ICONS));
    ULONG mn_show_all = wbMenuNumber(WBMENU_ID(WBMENU_WN__SHOW_ALL));

    // Sync menu checkmarks
    struct MenuItem *item_show_all = ItemAddress(my->Menu, mn_show_all);
    struct MenuItem *item_show_icons = ItemAddress(my->Menu, mn_show_icons);
    if (my->dd_Flags & DDFLAGS_SHOWALL) {
        item_show_icons->Flags &= ~CHECKED;
        item_show_all->Flags |= CHECKED;
    } else {
        item_show_icons->Flags |= CHECKED;
        item_show_all->Flags &= ~CHECKED;
    }

    vis = GetVisualInfo(my->Window->WScreen, TAG_END);
    LayoutMenus(my->Menu, vis, TAG_END);
    FreeVisualInfo(vis);

    SetMenuStrip(my->Window, my->Menu);

    /* Disable opening the parent for root window
     * and disk paths.
     */
    ULONG mn_open_parent = wbMenuNumber(WBMENU_ID(WBMENU_WN_OPEN_PARENT));
    ULONG mn_ic_copy = wbMenuNumber(WBMENU_ID(WBMENU_IC_COPY));
    ULONG mn_ic_format = wbMenuNumber(WBMENU_ID(WBMENU_IC_FORMAT));
    ULONG mn_ic_delete = wbMenuNumber(WBMENU_ID(WBMENU_IC_DELETE));
    ULONG mn_wn_show = wbMenuNumber(WBMENU_ID(WBMENU_WN__SHOW));
    ULONG mn_wn_view = wbMenuNumber(WBMENU_ID(WBMENU_WN__VIEW));
    if (my->Lock == BNULL) {
        OffMenu(my->Window, mn_open_parent);
        OffMenu(my->Window, mn_ic_copy);
        OffMenu(my->Window, mn_ic_delete);
        OnMenu(my->Window, mn_ic_format);
        OffMenu(my->Window, mn_wn_show);
        OffMenu(my->Window, mn_wn_view);
    } else {
        BPTR lock = ParentDir(my->Lock);
        if (lock == BNULL) {
            OffMenu(my->Window, mn_open_parent);
        } else {
            OnMenu(my->Window, mn_open_parent);
            UnLock(lock);
        }
        OnMenu(my->Window, mn_ic_copy);
        OnMenu(my->Window, mn_ic_delete);
        OffMenu(my->Window, mn_ic_format);
        OnMenu(my->Window, mn_wn_show);
        OnMenu(my->Window, mn_wn_view);
    }

    // Check for tools in the filesystem
    struct {
        int id;
        CONST_STRPTR path;
    } tools[] = {
        { WBMENU_ID(WBMENU_WB_CUST_UPDATER), "SYS:System/Updater" },
        { WBMENU_ID(WBMENU_WB_CUST_AMISTORE), "SYS:Utilities/Amistore" },
        { WBMENU_ID(WBMENU_IC_FORMAT), "SYS:System/Format" },
    };
    for (size_t n = 0; n < sizeof(tools)/sizeof(tools[0]); n++) {
        ULONG menu_number = wbMenuNumber(tools[n].id);
        if (menu_number != MENUNULL) {
            BPTR lock = Lock(tools[n].path, SHARED_LOCK);
            if (lock == BNULL) {
                OnMenu(my->Window, menu_number);
            } else {
                UnLock(lock);
            }
        }
    }

    RefreshGadgets(my->Window->FirstGadget, my->Window, NULL);

    // Now that we are ready to go, start watching this drawer.
    my->Notify.Request = (struct NotifyRequest){
        .nr_Name = my->Path,
        .nr_UserData = (IPTR)obj,
        .nr_Flags = NRF_SEND_MESSAGE  | NRF_NOTIFY_INITIAL,
    };
    my->Notify.Request.nr_stuff.nr_Msg.nr_Port = notifyport;
    my->Notify.Cached = FALSE;

    if (notifyport != NULL && StartNotify(&my->Notify.Request)) {
        D(bug("%s: StartNotify('%s') - '%s' actual.\n", __func__, my->Path, my->Notify.Request.nr_FullName));
    } else {
        D(bug("%s: Unable to StartNotify('%s') - faking it. (%ld)\n", __func__, my->Path, IoErr()));
        my->Notify.Request.nr_stuff.nr_Msg.nr_Port = NULL;
    }

    /* Send first intuitick */
    DoMethod(obj, WBWM_IntuiTick);

    return rc;

error:
    CoerceMethod(cl, obj, OM_DISPOSE, 0);

    return 0;
}

static IPTR WBWindow__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;

    if (my->Notify.Request.nr_stuff.nr_Msg.nr_Port != NULL) {
        EndNotify(&my->Notify.Request);
    }

    ClearMenuStrip(my->Window);
    FreeMenus(my->Menu);

    /* If we have a custom user port, be paranoid.
     * See the Autodocs for CloseWindow().
     */
    if (my->Window->UserPort) {
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

    if (my->Path) {
        FreeVec(my->Path);
    }

    if (my->Lock != BNULL) {
        UnLock(my->Lock);
    }

    return DoSuperMethodA(cl, obj, msg);
}

// OM_GET
static IPTR WBWindow__OM_GET(Class *cl, Object *obj, struct opGet *opg)
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
static IPTR WBWindow__OM_UPDATE(Class *cl, Object *obj, struct opUpdate *opu)
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

// WBWM_NewSize
static IPTR WBWindow__WBWM_NewSize(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct Window *win = my->Window;
    struct Region *clip;

    /* Clip to the window for drawing */
    clip = wbClipWindow(wb, win);
    wbWindowRedimension(cl, obj);
    wbUnclipWindow(wb, win, clip);

    return 0;
}

static IPTR WBWindow__WBWM_Refresh(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct Window *win = my->Window;

    // Gadtools window refresh.
    GT_BeginRefresh(win);
    GT_EndRefresh(win, TRUE);

    return 0;
}

static void wbWindowNewCLI(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);

    ASSERT_VALID_PROCESS(FindTask(NULL));

    SetWindowPointer(my->Window, WA_BusyPointer, TRUE, TAG_END);
    BPTR old = CurrentDir(my->Lock);
    Execute("", BNULL, BNULL);
    CurrentDir(old);
    SetWindowPointer(my->Window, WA_BusyPointer, FALSE, TAG_END);
}

static IPTR wbWindowSnapshot(Class *cl, Object *obj, BOOL all)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);

    // Snapshot myself.
    //
    // Note that we set ICONPUTA_OnlyUpdatePosition to FALSE, so that a new icon will be
    // created if needed to store the drawer data.
    struct DiskObject *diskObject = GetDiskObjectNew(my->Path);
    if (diskObject) {
        if (diskObject->do_DrawerData) {
            diskObject->do_DrawerData->dd_NewWindow.LeftEdge = my->Window->LeftEdge;
            diskObject->do_DrawerData->dd_NewWindow.TopEdge = my->Window->TopEdge;
            diskObject->do_DrawerData->dd_NewWindow.Width = my->Window->Width;
            diskObject->do_DrawerData->dd_NewWindow.Height = my->Window->Height;
            diskObject->do_DrawerData->dd_CurrentX = my->Window->LeftEdge;
            diskObject->do_DrawerData->dd_CurrentY = my->Window->TopEdge;
            diskObject->do_DrawerData->dd_Flags = my->dd_Flags;
            diskObject->do_DrawerData->dd_ViewModes = my->dd_ViewModes;
            PutIconTags(my->Path, diskObject, ICONPUTA_OnlyUpdatePosition, FALSE, TAG_END);
            FreeDiskObject(diskObject);
        }
    }

    if (all) {
        // Snapshot all icons.
        struct wbWindow_Icon *wbwi;
        ForeachNode(&my->IconList, wbwi) {
            DoMethod(wbwi->wbwiObject, WBIM_Snapshot);
        }
    }

    return 0;
}

// Returns count of selected objects.
static IPTR WBWindow__WBWM_ForSelected(Class *cl, Object *obj, struct wbwm_ForSelected *wbwmf)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct wbWindow_Icon *wbwi;
    IPTR count = 0;
    BOOL rescan = FALSE;

    BOOL modifier = (wbwmf->wbwmf_Msg->MethodID == WBIM_Rename ||
                     wbwmf->wbwmf_Msg->MethodID == WBIM_Copy   ||
                     wbwmf->wbwmf_Msg->MethodID == WBIM_Delete );

    if (modifier) {
        SetWindowPointer(my->Window, WA_BusyPointer, TRUE, WA_PointerDelay, TRUE, TAG_END);
    }
    ForeachNode(&my->IconList, wbwi) {
        IPTR selected = FALSE;
        GetAttr(GA_Selected, wbwi->wbwiObject, &selected);
        if (selected) {
            IPTR rc = DoMethodA(wbwi->wbwiObject, wbwmf->wbwmf_Msg);
            if (modifier && (rc & WBIF_REFRESH)) {
                rescan |= TRUE;
            }
            GetAttr(GA_Selected, wbwi->wbwiObject, &selected);
            if (!selected) {
                // Refresh the gadget.
                RefreshGList((struct Gadget *)wbwi->wbwiObject, my->Window, NULL, 1);
            }
            count += 1;
        }
    }
    if (modifier) {
        SetWindowPointer(my->Window, WA_BusyPointer, FALSE, TAG_END);
    }

    if (rescan) {
        if (my->Notify.Request.nr_stuff.nr_Msg.nr_Port==NULL) {
            CoerceMethod(cl, obj, WBWM_InvalidateContents);
            CoerceMethod(cl, obj, WBWM_CacheContents);
        }
    }

    D(bug("%s: %ld selected.\n", __func__, count));

    return count;
}


// WBWM_MenuPick
static IPTR WBWindow__WBWM_MenuPick(Class *cl, Object *obj, struct wbwm_MenuPick *wbwmp)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    struct MenuItem *item = wbwmp->wbwmp_MenuItem;
    BPTR lock;
    IPTR rc;

    switch (WBMENU_ITEM_ID(item)) {
    case WBMENU_ID(WBMENU_WN_OPEN_PARENT):
        if (my->Lock != BNULL) {
            lock = ParentDir(my->Lock);
            STRPTR path = wbAbspathLock(lock);
            if (path != NULL) {
                OpenWorkbenchObject(path, TAG_END);
                FreeVec(path);
            }
            UnLock(lock);
        }
        rc = 0;
        break;
    case WBMENU_ID(WBMENU_WN_SELECT_CONTENTS):
        DoGadgetMethod((struct Gadget *)my->Set, my->Window, NULL, (IPTR)WBSM_Select, NULL, (IPTR)TRUE);
        rc = 0;
        break;
    case WBMENU_ID(WBMENU_WN_CLEAN_UP):
        DoGadgetMethod((struct Gadget *)my->Set, my->Window, NULL, (IPTR)WBSM_Clean_Up, NULL);
        rc = 0;
        break;
    case WBMENU_ID(WBMENU_WN_UPDATE):
        rc = WBIF_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WN__SNAP_WINDOW):
        wbWindowSnapshot(cl, obj, FALSE);
        rc = 0;
        break;
    case WBMENU_ID(WBMENU_WN__SNAP_ALL):
        wbWindowSnapshot(cl, obj, TRUE);
        rc = 0;
        break;
    case WBMENU_ID(WBMENU_WN__SHOW_ICONS):
        my->dd_Flags = DDFLAGS_SHOWICONS;
        rc = WBIF_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WN__SHOW_ALL):
        my->dd_Flags = DDFLAGS_SHOWALL;
        rc = WBIF_REFRESH;
        break;
    case WBMENU_ID(WBMENU_WB_SHELL):
        wbWindowNewCLI(cl, obj);
        break;
    default:
        rc = 0;
        break;
    }

    if (rc & WBIF_REFRESH) {
        // Always force, regardless of notification mode.
        CoerceMethod(cl, obj, WBWM_InvalidateContents);
        CoerceMethod(cl, obj, WBWM_CacheContents);
    }

    return rc;
}

// WBWM_IntuiTick
static IPTR WBWindow__WBWM_IntuiTick(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbWindow *my = INST_DATA(cl, obj);
    IPTR rc = FALSE;

    BOOL changed = FALSE;
    ULONG avail = AvailMem(MEMF_CHIP) / 1024;
    if (my->AvailChip != avail) {
        my->AvailChip = avail;
        changed = TRUE;
    }
    avail = AvailMem(MEMF_FAST) / 1024;
    if (my->AvailFast != avail) {
        my->AvailFast = avail;
        changed = TRUE;
    }

    avail = AvailMem(MEMF_ANY) / 1024;
    if (my->AvailAny != avail) {
        my->AvailAny = avail;
        changed = TRUE;
    }

    if (changed) {
        IPTR val[6];

        val[0] = (IPTR)AS_STRING(WB_NAME);
        val[1] = WB_VERSION;
        val[2] = WB_REVISION;
        val[3] = my->AvailChip;
        val[4] = my->AvailFast;
        val[5] = my->AvailAny;

        /* Update the window's title */
        RawDoFmt("%s %ld.%ld  Chip: %ldk, Fast: %ldk, Any: %ldk", (RAWARG)val,
                 RAWFMTFUNC_STRING, my->ScreenTitle);

        SetWindowTitles(my->Window, (CONST_STRPTR)-1, my->ScreenTitle);
        rc = TRUE;
    }

    // Fake notifications
    CoerceMethod(cl, obj, WBWM_CacheContents);

    return rc;
}

static IPTR WBWindow_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    METHOD_CASE(WBWindow, OM_NEW);
    METHOD_CASE(WBWindow, OM_DISPOSE);
    METHOD_CASE(WBWindow, OM_GET);
    METHOD_CASE(WBWindow, OM_UPDATE);
    METHOD_CASE(WBWindow, WBWM_NewSize);
    METHOD_CASE(WBWindow, WBWM_MenuPick);
    METHOD_CASE(WBWindow, WBWM_IntuiTick);
    METHOD_CASE(WBWindow, WBWM_Refresh);
    METHOD_CASE(WBWindow, WBWM_ForSelected);
    METHOD_CASE(WBWindow, WBWM_InvalidateContents);
    METHOD_CASE(WBWindow, WBWM_CacheContents);
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
        cl->cl_Dispatcher.h_SubEntry = WBWindow_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
