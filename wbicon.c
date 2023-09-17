/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Icon Class
*/

#include <string.h>
#include <stdio.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/locale.h>
#include <proto/utility.h>
#ifdef __AROS__
#include <proto/workbench.h>
#else
#include <proto/wb.h>
#endif

#include <intuition/cghooks.h>
#include <dos/dostags.h>

#include "workbook_intern.h"
#include "wbcurrent.h"
#include "classes.h"

struct wbIcon {
    BPTR               ParentLock;
    STRPTR             File;
    struct DiskObject *DiskObject;
    STRPTR             Label;
    struct Screen     *Screen;

    struct Rectangle  HitBox;  // Icon image hit box, which does not include label.
    BOOL ListView;
    IPTR ListLabelWidth;
    struct IntuiText ListILabel;
    struct IntuiText ListIMeta;
    ULONG Protection;
    ULONG Size;
    struct DateStamp DateStamp;
    char ListLabelMeta[/* size */ 6 + 1 + /* prot */ 8 + 1 + 20 + 1];

    struct timeval LastActive;
};

static const struct TagItem wbIcon_DrawTags[] = {
    { ICONDRAWA_Frameless, FALSE, },
    { ICONDRAWA_Borderless, FALSE, },
    { ICONDRAWA_EraseBackground, TRUE, },
    { TAG_END },
};

#ifndef __amigaos4__
// Shim function from AmigaOS 4 API.
#define DN_DEVICEONLY 1
static BOOL _DevNameFromLock(struct WorkbookBase *wb, BPTR lock, STRPTR buffer, size_t size, ULONG flags)
{
    if (lock == 0) {
        return FALSE;
    }

    struct DosInfo * di = (struct DosInfo *)BADDR(((struct DosLibrary *)DOSBase)->dl_Root->rn_Info);
    struct FileLock * fl = (struct FileLock *)BADDR(lock);
    struct DevInfo * dvi;

    BOOL ok = FALSE;
    Forbid();
    for(dvi = (struct DevInfo *)BADDR(di->di_DevInfo) ;
        dvi != NULL ;
        dvi = (struct DevInfo *)BADDR(dvi->dvi_Next)) {
        if(dvi->dvi_Type == DLT_DEVICE && dvi->dvi_Task == fl->fl_Task) {
           UBYTE * name = AROS_BSTR_ADDR(dvi->dvi_Name);
           size_t name_len = AROS_BSTR_strlen(dvi->dvi_Name);
           if (name_len + 1 + 1 > size) {
               break;
           }

           CopyMem(name, buffer, name_len);
           buffer[name_len++] = ':';
           buffer[name_len++] = 0;
           ok = TRUE;
           break;
        }
     }

     Permit();

     return ok;
}
#define DevNameFromLock(a, b, c, d) _DevNameFromLock(wb, a, b, c, d)
#endif // !__amigaos4__

static void wbIcon_UpdateAsList(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    struct DrawInfo *dri = GetScreenDrawInfo(my->Screen);
    if (!dri) {
        return;
    }

    my->ListILabel = (struct IntuiText){
        .FrontPen = dri->dri_Pens[TEXTPEN],
        .BackPen = dri->dri_Pens[BACKGROUNDPEN],
        .DrawMode = JAM2,
        .LeftEdge = 0,
        .TopEdge = 0,
        .ITextFont = my->Screen->Font,
        .IText = my->Label,
    };
    my->ListIMeta = (struct IntuiText){
        .FrontPen = dri->dri_Pens[TEXTPEN],
        .BackPen = dri->dri_Pens[BACKGROUNDPEN],
        .DrawMode = JAM2,
        .LeftEdge = 0,
        .TopEdge = 0,
        .ITextFont = my->Screen->Font,
        .IText = my->ListLabelMeta,
    };

    my->HitBox = (struct Rectangle){
        .MaxX = IntuiTextLength(&my->ListILabel),
        .MaxY = my->Screen->Font->ta_YSize,
    };

    D(bug("%s: %s %s [hitbox (%ld,%ld)-(%ld,%ld)]\n",
                my->File, my->Label, my->ListLabelMeta,
                (IPTR)my->HitBox.MinX, (IPTR)my->HitBox.MinY,
                (IPTR)my->HitBox.MaxX, (IPTR)my->HitBox.MaxY));

    // FIXME: This should be from the (fixed width) screen font.
    const LONG ta_XSize = 8;

    SetAttrs(obj,
            GA_Width, (my->ListLabelWidth + STRLEN(my->ListLabelMeta))*ta_XSize,
            GA_Height, my->Screen->Font->ta_YSize,
            TAG_END);

    FreeScreenDrawInfo(my->Screen, dri);
}

static void wbIcon_UpdateAsIcon(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    struct Rectangle rect;
    UWORD icon_w,icon_h;

    /* Update the parent's idea of how big we are with labels.
     */
    GetIconRectangleA(&my->Screen->RastPort, my->DiskObject, (STRPTR)my->Label, &rect, (struct TagItem *)wbIcon_DrawTags);

    icon_w = (rect.MaxX - rect.MinX) + 1;
    icon_h = (rect.MaxY - rect.MinY) + 1;

    // Get the hit box (image without label)
    GetIconRectangleA(&my->Screen->RastPort, my->DiskObject, NULL, &my->HitBox, (struct TagItem *)wbIcon_DrawTags);
    UWORD image_w = (my->HitBox.MaxX - rect.MinX) + 1;
    if (icon_w > image_w) {
        // Label bigger than icon? Move the hitbox to the center.
        my->HitBox.MinX += (icon_w - image_w) / 2;
        my->HitBox.MaxX += (icon_w - image_w) / 2;
    }

    D(bug("%s: %ldx%ld @%ld,%ld [hitbox (%ld,%ld)-(%ld,%ld)] (%s)\n",
                my->File, (IPTR)icon_w, (IPTR)icon_h,
                (IPTR)my->DiskObject->do_CurrentX, (IPTR)my->DiskObject->do_CurrentY,
                (IPTR)my->HitBox.MinX, (IPTR)my->HitBox.MinY,
                (IPTR)my->HitBox.MaxX, (IPTR)my->HitBox.MaxY,
                my->Label));

    SetAttrs(obj,
        GA_Width, icon_w,
        GA_Height, icon_h,
        TAG_END);
}

static void wbIcon_Update(Class *cl, Object *obj)
{
    struct wbIcon *my = INST_DATA(cl, obj);

    if (my->ListView) {
        wbIcon_UpdateAsList(cl, obj);
    } else {
        wbIcon_UpdateAsIcon(cl, obj);
    }
}

static AROS_UFH3(void, wbIcon_LocalePutChar,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Locale *, locale, A2),
    AROS_UFHA(void *, c, A1))
{
    AROS_USERFUNC_INIT

    STRPTR cp = hook->h_Data;
    *(cp++) = (char)(IPTR)c;
    *cp = 0;
    hook->h_Data = cp;

    AROS_USERFUNC_EXIT
}


// OM_NEW
static IPTR WBIcon__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;

    STRPTR file = (STRPTR)GetTagData(WBIA_File, (IPTR)NULL, ops->ops_AttrList);
    STRPTR label= (STRPTR)GetTagData(WBIA_Label, (IPTR)NULL, ops->ops_AttrList);
    BPTR parentlock   = (BPTR)GetTagData(WBIA_ParentLock, (IPTR)BNULL, ops->ops_AttrList);
    struct Screen *screen = (struct Screen *)GetTagData(WBIA_Screen, (IPTR)NULL, ops->ops_AttrList);
    BOOL listview = (BOOL)GetTagData(WBIA_ListView, (IPTR)FALSE, ops->ops_AttrList);
    ULONG listlabelwidth = (ULONG)GetTagData(WBIA_ListLabelWidth, (IPTR)15, ops->ops_AttrList);
    ULONG protection;
    ULONG size;
    struct DateStamp datestamp;

    if (!file) {
        return 0;
    }

    struct DiskObject *diskobject = NULL;
    BPTR old = CurrentDir(parentlock);
    BPTR lock = Lock(file, SHARED_LOCK);
    BOOL ok = FALSE;
    if (lock != BNULL) {
        struct FileInfoBlock *fib = AllocDosObjectTags(DOS_FIB, TAG_END);
        if (fib != NULL) {
            if (Examine(lock, fib)) {
                protection = (ULONG)fib->fib_Protection;
                size = (ULONG)fib->fib_Size;
                datestamp = fib->fib_Date;
                ok = TRUE;
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    }
    if (ok) {
        diskobject = GetIconTags(file,
                               ICONGETA_Screen, screen,
                               ICONGETA_FailIfUnavailable, FALSE,
                               TAG_END);
    }
    CurrentDir(old);
    if (diskobject == NULL) {
        return 0;
    }

    file = StrDup(file);
    if (!file) {
        FreeDiskObject(diskobject);
        return 0;
    }

    if (label == NULL) {
        label = file;
    }
    label = StrDup(label);
    if (label == NULL) {
        FreeVec(file);
        FreeDiskObject(diskobject);
        return 0;
    }

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)ops);
    if (obj == NULL) {
        FreeVec(label);
        FreeVec(file);
        FreeDiskObject(diskobject);
        return 0;
    }

    struct wbIcon *my = INST_DATA(cl, obj);

    my->File = file;
    my->Label = label;
    my->ParentLock = parentlock;
    my->DiskObject = diskobject;
    my->Screen = screen;

    my->ListView = listview;
    my->ListLabelWidth = listlabelwidth;
    my->Protection = protection;
    my->Size = size;
    my->DateStamp = datestamp;
    my->ListILabel = (struct IntuiText){0};
    my->ListIMeta  = (struct IntuiText){0};

    char prottext[9]={0};
    CONST_STRPTR protbits = "xsparwed";
    for (int i = 0; i < 8; i++) {
        // Lower 4 bits are inverted for display purposes.
        if ((my->Protection ^ 0xf) & (1 << i)) {
            prottext[7-i] = protbits[7-i];
        } else {
            prottext[7-i] = '-';
        }
    }

    if (my->DiskObject->do_Type == WBDRAWER || my->DiskObject->do_Type == WBGARBAGE) {
        IPTR val[] = {
            (IPTR)prottext,
        };
        RawDoFmt("Drawer %s ", (RAWARG)val, RAWFMTFUNC_STRING, my->ListLabelMeta);
    } else {
        if (size <= 999999) {
            IPTR val[] = {
                (IPTR)size,
                (IPTR)prottext,
            };
            RawDoFmt("%6ld %s ", (RAWARG)val, RAWFMTFUNC_STRING, my->ListLabelMeta);
        } else if (size < 999 * 1000 * 1000) {
            IPTR val[] = {
                (IPTR)(size / 1000 / 1000),
                (IPTR)((size / 1000 / 100) % 10),
                (IPTR)prottext,
            };
            RawDoFmt("%3ld.%ldM %s ", (RAWARG)val, RAWFMTFUNC_STRING, my->ListLabelMeta);
        } else {
            IPTR val[] = {
            (IPTR)(size / 1000 / 1000 / 1000),
            (IPTR)((size / 1000 / 1000) % 1000),
            (IPTR)prottext,
            };
            RawDoFmt("%ld.%03ldG %s ", (RAWARG)val, RAWFMTFUNC_STRING, my->ListLabelMeta);
        }
    }
    struct Hook datehook = {
        .h_Entry = (ULONG(*)())wbIcon_LocalePutChar,
        .h_Data = &my->ListLabelMeta[6 + 1 + 8 + 1],
    };
    struct Locale *locale = OpenLocale(NULL);
    if (locale) {
        FormatDate(locale, "%d-%b-%Y %X", &my->DateStamp, &datehook);
        CloseLocale(locale);
    }

    wbIcon_Update(cl, obj);

    return (IPTR)obj;
}

// OM_DISPOSE
static IPTR WBIcon__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    ASSERT(my->Label != NULL);
    FreeVec(my->Label);

    ASSERT(my->File != NULL);
    FreeVec(my->File);

    ASSERT(my->DiskObject != NULL);
    FreeDiskObject(my->DiskObject);

    return DoSuperMethodA(cl, obj, msg);
}

// OM_GET
static IPTR WBIcon__OM_GET(Class *cl, Object *obj, struct opGet *opg)
{
    struct wbIcon *my = INST_DATA(cl, obj);
    IPTR rc = TRUE;

    switch (opg->opg_AttrID) {
    case WBIA_File:
        *(opg->opg_Storage) = (IPTR)my->File;
        break;
    case WBIA_Label:
        *(opg->opg_Storage) = (IPTR)my->Label;
        break;
    case WBIA_ParentLock:
        *(opg->opg_Storage) = (IPTR)my->ParentLock;
        break;
    case WBIA_Size:
        *(opg->opg_Storage) = (IPTR)my->Size;
        break;
    case WBIA_Protection:
        *(opg->opg_Storage) = (IPTR)my->Protection;
        break;
    case WBIA_DateStamp:
        *(struct DateStamp *)(opg->opg_Storage) = my->DateStamp;
        break;
    case WBIA_Type:
        *(opg->opg_Storage) = (IPTR)my->DiskObject->do_Type;
        break;
    case WBIA_CurrentX:
        *(opg->opg_Storage) = (IPTR)my->DiskObject->do_CurrentX;
        break;
    case WBIA_CurrentY:
        *(opg->opg_Storage) = (IPTR)my->DiskObject->do_CurrentY;
        break;
    case WBIA_HitBox:
        *(struct Rectangle *)(opg->opg_Storage) = my->HitBox;
        break;
    default:
        rc = DoSuperMethodA(cl, obj, (Msg)opg);
        break;
    }

    return rc;
}

// OM_SET
static IPTR WBIcon__OM_SET(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    struct TagItem *tags = ops->ops_AttrList;
    struct TagItem *ti;

    BOOL render = FALSE;
    BOOL listview = my->ListView;
    ULONG listlabelwidth = my->ListLabelWidth;

    while ((ti = NextTagItem(&tags)) != NULL) {
        switch (ti->ti_Tag) {
        case GA_Selected:
            // Re-render if this attribute is present.
            render = TRUE;
            break;
        case WBIA_ListView:
            listview = (BOOL)ti->ti_Data;
            break;
        case WBIA_ListLabelWidth:
            listlabelwidth = (BOOL)ti->ti_Data;
            break;
        case WBIA_CurrentX:
            my->DiskObject->do_CurrentX = (LONG)ti->ti_Data;
            break;
        case WBIA_CurrentY:
            my->DiskObject->do_CurrentY = (LONG)ti->ti_Data;
            break;
        }
    }

    BOOL update = FALSE;
    if (listview != my->ListView) {
        my->ListView = listview;
        update = TRUE;
    }
    if (listlabelwidth != my->ListLabelWidth) {
        my->ListLabelWidth = listlabelwidth;
        if (my->ListView) {
            update = TRUE;
        }
    }

    if (update) {
        wbIcon_Update(cl, obj);
    }

    render |= DoSuperMethodA(cl, obj, (Msg)ops);

    return render;
}

// GM_RENDER
static IPTR WBIcon__GM_RENDER(Class *cl, Object *obj, struct gpRender *gpr)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    struct RastPort *rp = gpr->gpr_RPort;
    struct Window *win = gpr->gpr_GInfo->gi_Window;
    struct Region *clip;
    struct Gadget *gadget = (struct Gadget *)obj;       /* Legal for 'gadgetclass' */
    WORD x,y;

    x = gadget->LeftEdge;
    y = gadget->TopEdge;

    if (rp == NULL) {
        rp = ObtainGIRPort(gpr->gpr_GInfo);
    }

    if (rp) {
        /* Clip to the window for drawing */
        clip = wbClipWindow(wb, win);
        if (my->ListView) {
            struct IntuiText label = my->ListILabel;
            if (gadget->Flags & GFLG_SELECTED) {
                label.FrontPen = my->ListILabel.BackPen;
                label.BackPen = my->ListILabel.FrontPen;
            }
            PrintIText(rp, &label, x, y);
            PrintIText(rp, &my->ListIMeta, x + 8 * my->ListLabelWidth, y);
        } else {
            DrawIconStateA(rp, my->DiskObject, (STRPTR)my->Label, x, y,
                (gadget->Flags & GFLG_SELECTED) ? IDS_SELECTED : IDS_NORMAL, (struct TagItem *)wbIcon_DrawTags);
        }
        wbUnclipWindow(wb, win, clip);

        if (gpr->gpr_RPort == NULL) {
            ReleaseGIRPort(rp);
        }
    }

    return 0;
}

// If multiple are selected, clear all and mark this as selected.
// If none are selected or only this is selected, clear all and toggle selection mark.
// If shift-selecting, toggle selection mark.
//
// Returns new selection state
static BOOL wbIconToggleSelected(Class *cl, Object *obj, struct GadgetInfo *gi, UWORD qualifier)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    IPTR count = 0;

    // Toggle selection
    IPTR selected = FALSE;
    GetAttr(GA_Selected, obj, &selected);

    IPTR msg[] = { OM_Dummy };
    count = DoMethod(wb->wb_App, WBAM_ForSelected, (Msg)msg);

    // Are we in shift-select mode?
    BOOL shift_select = (qualifier & (IEQUALIFIER_LSHIFT | IEQUALIFIER_LSHIFT)) == 1;

    BOOL deselect;

    if (!shift_select) {
        // Normal select mode
        if (count == 0) {
            // If none are selected, set outselves as selected.
            selected = TRUE;
            deselect = FALSE;
        } else if (count == 1) {
            if (!selected) {
                // If one is selected, and we are not selected, deselect all and set outselves as selected.
                selected = TRUE;
                deselect = TRUE;
            } else {
                // If one is selected, and we are selected, set outselves as deselected.
                selected = FALSE;
                deselect = FALSE;
            }
        } else {
            if (!selected) {
                // If many are selected, and we are not selected, deselect all and set outselves as selected.
                selected = TRUE;
                deselect = TRUE;
            } else {
                // If many are selected, and we are selected, deselect all and set outselves as selected.
                selected = TRUE;
                deselect = TRUE;
            }
        }
    } else {
        // Shift-select mode
        if (count == 0) {
            // If none are selected, set outselves as selected.
            selected = TRUE;
            deselect = FALSE;
        } else {
            if (!selected) {
                // If any are selected, and we are not selected, set outselves as selected.
                selected = TRUE;
                deselect = FALSE;
            } else {
                // If any are selected, and we are selected, set outselves as not selected.
                selected = FALSE;
                deselect = FALSE;
            }
        }
    }

    if (deselect) {
        // De-select all items.
        DoMethod(wb->wb_App, WBAM_ClearSelected);
    }

    SetAttrs(obj, GA_Selected, selected, TAG_END);

    // Redraw
    struct RastPort *rp;
    if ((rp = ObtainGIRPort(gi)) != NULL) {
        DoMethod(obj, GM_RENDER, gi, rp, GREDRAW_TOGGLE);
        ReleaseGIRPort(rp);
    }

    return selected;
}

// GM_HITTEST
static IPTR WBIcon__GM_HITTEST(Class *cl, Object *obj, struct gpHitTest *gpht) {
    struct wbIcon *my = INST_DATA(cl, obj);

    IPTR rc = 0;

    if (gpht->gpht_Mouse.X >= my->HitBox.MinX && gpht->gpht_Mouse.X <= my->HitBox.MaxX &&
        gpht->gpht_Mouse.Y >= my->HitBox.MinY && gpht->gpht_Mouse.Y <= my->HitBox.MaxY) {
        rc = GMR_GADGETHIT;
    }

    return rc;
}

// GM_GOACTIVE
static IPTR WBIcon__GM_GOACTIVE(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    IPTR rc = GMR_NOREUSE;

    /* On a double-click, don't go 'active', just
     * do the action.
     */
    BOOL dclicked = DoubleClick(my->LastActive.tv_secs,
                               my->LastActive.tv_micro,
                               gpi->gpi_IEvent->ie_TimeStamp.tv_secs,
                               gpi->gpi_IEvent->ie_TimeStamp.tv_micro);

    my->LastActive = gpi->gpi_IEvent->ie_TimeStamp;

    if (dclicked)
    {
        D(bug("%s: Double-clicked => %lx\n", __func__, (IPTR)wb->wb_App));

        SetAttrs(obj, GA_Selected, TRUE, TAG_END);
        IPTR msg[] = { WBIM_Open };
        DoMethod(wb->wb_App, WBAM_ForSelected, (Msg)msg);
        // De-select all.
        DoMethod(wb->wb_App, WBAM_ClearSelected);
    } else {
        if (gpi->gpi_IEvent != NULL) {
            my->LastActive = gpi->gpi_IEvent->ie_TimeStamp;

            // Select this item.
            wbIconToggleSelected(cl, obj, gpi->gpi_GInfo, gpi->gpi_IEvent->ie_Qualifier);

            // Notify that drag/drop should start.
            DoMethod(wb->wb_App, WBAM_DragDropBegin);
            rc = GMR_MEACTIVE;
        }
    }

    return rc;
}

// GM_HANDLEINPUT
static IPTR WBIcon__GM_HANDLEINPUT(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct InputEvent *iev = gpi->gpi_IEvent;

    IPTR rc = GMR_MEACTIVE;

    if (iev->ie_Class == IECLASS_RAWMOUSE) {
        switch (iev->ie_Code) {
        case IECODE_NOBUTTON:
            DoMethod(wb->wb_App, WBAM_DragDropUpdate);
            break;
        case SELECTUP:
            rc = GMR_REUSE;
            break;
        case MENUDOWN:
            rc = GMR_REUSE;
            break;
        default:
            rc = GMR_MEACTIVE;
            break;
        }
    }

    return rc;
}

// GM_GOACTIVE
static IPTR WBIcon__GM_GOINACTIVE(Class *cl, Object *obj, struct gpGoInactive *gpgi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;

    D(bug("%s: %lx\n", __func__, (IPTR)obj));

    // Turn off the DnD manager.
    DoMethod(wb->wb_App, WBAM_DragDropEnd);

    return 0;
}

// WBIM_Open
static IPTR WBIcon__WBIM_Open(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    D(struct Process *proc =) CreateNewProcTags(
            NP_Name, (IPTR)my->File,
            NP_Entry, (IPTR)wbOpener,
            NP_CurrentDir, (IPTR)my->ParentLock,
            TAG_END);
    D(bug("WBIcon.Open: %s via %lx\n", my->File, (IPTR)proc));

    return 0;
}

// WBIM_Copy
static IPTR WBIcon__WBIM_Copy(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    BOOL ok;

    // Is this object suitable for a bump copy?
    switch (my->DiskObject->do_Type) {
    case WBTOOL:
        // fallthrough
    case WBPROJECT:
        // fallthrough
    case WBDRAWER:
        // Things we can copy.
        ok = TRUE;
        break;
    case WBKICK:
        // fallthrough
    case WBDISK:
        // fallthrough
    case WBGARBAGE:
        // Things we can't copy.
        ok = FALSE;
        break;
    }

    if (ok) {
        BPTR pwd = CurrentDir(my->ParentLock);
        ok = wbCopyBumpCurrent(my->File);
        if (!ok) {
            wbPopupIoErr(wb, "Copy", IoErr(), my->File);
        }
        CurrentDir(pwd);
    }

    return ok;
}

static BOOL rename_path(struct WorkbookBase *wb, CONST_STRPTR file, CONST_STRPTR input, CONST_STRPTR suffix)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    BOOL ok;

    IPTR suffix_len = STRLEN(suffix);
    IPTR file_len = STRLEN(file);
    IPTR path_len = PathPart(file) - file;
    IPTR oldname_len = path_len + 1 + STRLEN(FilePart(file)) + suffix_len + 1;
    IPTR newname_len = path_len + 1 + STRLEN(input) + suffix_len + 1;
    STRPTR oldname = AllocVec(oldname_len + newname_len, MEMF_ANY);
    STRPTR newname = &oldname[oldname_len];

    // Construct old name
    CopyMem(file, oldname, file_len);
    CopyMem(suffix, &oldname[file_len], suffix_len + 1);

    // Construct new name
    CopyMem(file, newname, path_len);
    newname[path_len] = 0;
    AddPart(newname, input, newname_len);
    CopyMem(suffix, &newname[STRLEN(newname)], suffix_len + 1);

    // Return ok if we have a suffix, but the oldname doesn't exist.
    if (suffix[0] != 0) {
        BPTR lock = Lock(oldname, ACCESS_READ);
        if (!lock) {
            ok = TRUE;
        } else {
            UnLock(lock);
            ok = Rename(oldname, newname);
        }
    } else {
        D(bug("%s: Rename '%s' => '%s'\n", __func__, oldname, newname));
        ok = Rename(oldname, newname);
    }

    FreeVec(oldname);

    return ok;
}

static IPTR rename_action(struct WorkbookBase *wb, CONST_STRPTR input, APTR arg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct wbIcon *my = arg;
    BOOL ok;

    LONG len = STRLEN(my->File);
    if (my->File[len-1] == ':') {
        BPTR lock = Lock(my->File, SHARED_LOCK);
        ok = FALSE;
        if (!lock) {
            D(bug("%s: Can't get lock\n", my->File));
        } else {
            char buff[256];
            if (!DevNameFromLock(lock, buff, sizeof(buff), DN_DEVICEONLY)) {
                UnLock(lock);
                D(bug("%s: Can't get name from lock.\n", my->File));
            } else {
                UnLock(lock);
                ok = Relabel(buff, input);
                if (!ok) {
                    wbPopupIoErr(wb, "Relabel", IoErr(), my->File);
                }
            }
        }
    } else {
        BPTR oldLock = CurrentDir(my->ParentLock);
        ok = rename_path(wb, my->File, input, "");
        if (ok) {
            ok = rename_path(wb, my->File, input, ".info");
            if (!ok) {
                // Revert the underlying file's rename.
                rename_path(wb, input, my->File, "");
                wbPopupIoErr(wb, "Relabel (.info)", IoErr(), my->File);
            }
        } else {
            wbPopupIoErr(wb, "Relabel", IoErr(), my->File);
        }
        CurrentDir(oldLock);
    }

    return ok;
}

// WBIM_Rename
static IPTR WBIcon__WBIM_Rename(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    ULONG len = STRLEN(my->File);
    IPTR ok = FALSE;

    if (len > 0 && my->File[len-1] == ':') {
        // Volume rename
        ok = wbPopupAction(wb, "Relabel", "Enter a new volume name.", "New Name:", my->Label, 0, ":/", rename_action, my);
    } else {
        ok = wbPopupAction(wb, "Rename", "Enter a new file name.", "New Name:", my->File, 0, ":/", rename_action, my);
    }

    return ok ? WBIF_REFRESH : 0;
}

// WBIM_Info
static IPTR WBIcon__WBIM_Info(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    BPTR lock;

    BPTR oldLock = CurrentDir(my->ParentLock);
    lock = Lock(my->File, SHARED_LOCK);
    if (lock == BNULL) {
        wbPopupIoErr(wb, "Info", IoErr(), my->File);
    } else {
        // We'd like to use 'WBInfo', but the AROS ROMs directly call WANDERER:Tools/Info.
        // Sigh.
        WBInfo(lock, my->File, my->Screen);
        UnLock(lock);
    }
    CurrentDir(oldLock);

    return 0;
}

// WBIM_Snapshot
static IPTR WBIcon__WBIM_Snapshot(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    D(bug("%s: %s\n", __func__, my->File));

    BPTR oldLock = CurrentDir(my->ParentLock);
    PutIconTags(my->File, my->DiskObject, ICONPUTA_OnlyUpdatePosition, TRUE, TAG_END);
    CurrentDir(oldLock);

    return 0;
}

// WBIM_Unsnapshot
static IPTR WBIcon__WBIM_Unsnapshot(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    D(bug("%s: %s\n", __func__, my->File));

    my->DiskObject->do_CurrentX = (LONG)NO_ICON_POSITION;
    my->DiskObject->do_CurrentY = (LONG)NO_ICON_POSITION;

    BPTR oldLock = CurrentDir(my->ParentLock);
    PutIconTags(my->File, my->DiskObject, ICONPUTA_OnlyUpdatePosition, TRUE, TAG_END);
    CurrentDir(oldLock);

    return 0;
}

// WBIM_Leave_Out
static IPTR WBIcon__WBIM_Leave_Out(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    return 0;
}

// WBIM_Put_Away
static IPTR WBIcon__WBIM_Put_Away(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    return 0;
}

// WBIM_Delete
static IPTR WBIcon__WBIM_Delete(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    BOOL ok;
    LONG err;

    // Is this object suitable for a delete?
    switch (my->DiskObject->do_Type) {
    case WBTOOL:
        // fallthrough
    case WBPROJECT:
        // fallthrough
    case WBDRAWER:
        // Things we can copy.
        ok = TRUE;
        break;
    case WBKICK:
        // fallthrough
    case WBDISK:
        // fallthrough
    case WBGARBAGE:
        // Things we can't delete.
        ok = FALSE;
        err = ERROR_OBJECT_WRONG_TYPE;
        break;
    }

    if (ok && my->ParentLock == BNULL) {
        // Unable to root window items.
        ok = FALSE;
        err = ERROR_OBJECT_WRONG_TYPE;
    }

    if (ok) {
        BPTR pwd = CurrentDir(my->ParentLock);
        ok = wbDeleteFromCurrent(my->File, FALSE);
        err = IoErr();
        CurrentDir(pwd);
    }

    if (!ok) {
        wbPopupIoErr(wb, "Delete", err, my->File);
    }

    return ok ? WBIF_REFRESH : 0;
}

// WBwbwiAppendIM_Format
static IPTR WBIcon__WBIM_Format(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    BPTR lock = Lock(my->File, ACCESS_READ);
    BOOL ok = FALSE;
    if (OpenWorkbenchObject("SYS:System/Format",
                WBOPENA_ArgLock, lock,
                WBOPENA_ArgName, (lock != BNULL) ? (CONST_STRPTR)"" : (CONST_STRPTR)my->File,
                TAG_END)) {
        ok = TRUE;
    } else {
        D(bug("%s: Can't run SYS:System/Format.\n", my->File));
    }
    UnLock(lock);

    if (!ok) {
        wbPopupIoErr(wb, "Format", 0, my->File);
    }

    return 0;
}

// WBIM_Empty_Trash
static IPTR WBIcon__WBIM_Empty_Trash(Class *cl, Object *obj, Msg msg)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    return 0;
}

// WBIM_DragDropAdd
static IPTR WBIcon__WBIM_DragDropAdd(Class *cl, Object *obj, struct wbimd_DragDropAdd *wbimd)
{
    struct wbIcon *my = INST_DATA(cl, obj);
    struct Gadget *gadget = (struct Gadget *)obj;

    // Adjust Top, Left by Window coordinates
    D(bug("%s: gadget @(%ld,%ld), window @(%ld,%ld)\n", __func__, (IPTR)gadget->LeftEdge, (IPTR)gadget->TopEdge, (IPTR)wbimd->wbimd_GInfo->gi_Window->LeftEdge, (IPTR)wbimd->wbimd_GInfo->gi_Window->TopEdge));
    struct Rectangle rect = {
        .MinX = gadget->LeftEdge + my->HitBox.MinX + wbimd->wbimd_GInfo->gi_Window->LeftEdge,
        .MinY = gadget->TopEdge + my->HitBox.MinY + wbimd->wbimd_GInfo->gi_Window->TopEdge,
        .MaxX = gadget->LeftEdge + my->HitBox.MaxX + wbimd->wbimd_GInfo->gi_Window->LeftEdge,
        .MaxY = gadget->TopEdge + my->HitBox.MaxY + wbimd->wbimd_GInfo->gi_Window->TopEdge,
    };

    D(bug("%s: Rectangle (%ld,%ld)-(%ldx%ld)\n", __func__, (IPTR)rect.MinX, (IPTR)rect.MinY, (IPTR)rect.MaxX, (IPTR)rect.MaxY));

    return DoMethod(wbimd->wbimd_DragDrop, WBDM_Add, (IPTR)WBDT_RECTANGLE, (IPTR)&rect);
}

static IPTR WBIcon__WBxM_DragDropped(Class *cl, Object *obj, struct wbxm_DragDropped *wbxmd)
{
    ASSERT_VALID_PROCESS((struct Process *)FindTask(NULL));

    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    D(bug("%s: DragDrop accepted by file %s\n", __func__, my->File));

    IPTR arg_count = DoMethod(wb->wb_App, WBAM_ReportSelected, NULL);
    if (arg_count < 2) {
        // Nothing selected.
        return TRUE;
    }

    BOOL ok = FALSE;
    struct TagItem *args = NULL;
    arg_count = DoMethod(wb->wb_App, WBAM_ReportSelected, &args);
    D(bug("%s: %ld tags in report\n", __func__, arg_count));
    if (arg_count == 0) {
        // Unable to collect report.
        return FALSE;
    }

    LONG err = 0;

    BPTR lock = BNULL;
    BPTR oldLock = CurrentDir(my->ParentLock);
    switch (my->DiskObject->do_Type) {
    case WBTOOL:
        // fallthrough
    case WBPROJECT:
        // Can we just drop the tags directly?
        D(bug("%s: Drop all args onto %s\n", __func__, my->File));
        ok = OpenWorkbenchObjectA(my->File, args);
        err = IoErr();
        break;
    case WBKICK:
        // fallthrough
    case WBDISK:
        // fallthrough
    case WBGARBAGE:
        // fallthrough
    case WBDRAWER:
        // Move/Copy source items into the location
        D(bug("%s: Drag all args into %s\n", __func__, my->File));
        D(wbDebugReportSelected(wb));
        lock = Lock(my->File, SHARED_LOCK);
        if (lock != BNULL) {
            CurrentDir(lock);
            ok = wbDropOntoCurrent(args);
            err = IoErr();
            UnLock(lock);
        } else {
            ok = FALSE;
            err = IoErr();
        }
        break;
    default:
        ok = FALSE;
        err = ERROR_OBJECT_WRONG_TYPE;
        break;
    }
    CurrentDir(oldLock);

    FreeTagItems(args);

    if (!ok) {
        wbPopupIoErr(wb, "Icon Drag/Drop", err, my->File);
    }

    return ok;
}

static IPTR WBIcon_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    METHOD_CASE(WBIcon, OM_NEW);
    METHOD_CASE(WBIcon, OM_DISPOSE);
    METHOD_CASE(WBIcon, OM_GET);
    METHOD_CASE(WBIcon, OM_SET);
    METHOD_CASE(WBIcon, GM_RENDER);
    METHOD_CASE(WBIcon, GM_HITTEST);
    METHOD_CASE(WBIcon, GM_GOACTIVE);
    METHOD_CASE(WBIcon, GM_HANDLEINPUT);
    METHOD_CASE(WBIcon, GM_GOINACTIVE);
    METHOD_CASE(WBIcon, WBIM_Open);
    METHOD_CASE(WBIcon, WBIM_Copy);
    METHOD_CASE(WBIcon, WBIM_Rename);
    METHOD_CASE(WBIcon, WBIM_Info);
    METHOD_CASE(WBIcon, WBIM_Snapshot);
    METHOD_CASE(WBIcon, WBIM_Unsnapshot);
    METHOD_CASE(WBIcon, WBIM_Leave_Out);
    METHOD_CASE(WBIcon, WBIM_Put_Away);
    METHOD_CASE(WBIcon, WBIM_Delete);
    METHOD_CASE(WBIcon, WBIM_Format);
    METHOD_CASE(WBIcon, WBIM_Empty_Trash);
    METHOD_CASE(WBIcon, WBIM_DragDropAdd);
    METHOD_CASE(WBIcon, WBxM_DragDropped);
    default:               rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBIcon_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "gadgetclass", NULL,
                    sizeof(struct wbIcon),
                    0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBIcon_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
