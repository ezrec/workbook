/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Icon Class
*/

#include <string.h>
#include <stdio.h>

#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/gadtools.h>
#include <proto/utility.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/graphics.h>
#ifdef __AROS__
#include <proto/workbench.h>
#else
#include <proto/wb.h>
#endif

#include <intuition/cghooks.h>
#include <dos/dostags.h>

#include "workbook_intern.h"
#include "classes.h"

struct wbIcon {
    STRPTR             File;
    struct DiskObject *DiskObject;
    STRPTR             Label;
    struct Screen     *Screen;
    Object            *Set;

    struct Rectangle  HitBox;  // Icon image hit box, which does not include label.

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


static void wbIcon_Update(Class *cl, Object *obj)
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
                my->File, icon_w, icon_h,
                my->DiskObject->do_CurrentX, my->DiskObject->do_CurrentY,
                my->HitBox.MinX, my->HitBox.MinY,
                my->HitBox.MaxX, my->HitBox.MaxY,
                my->Label));

    SetAttrs(obj,
        GA_Left, (my->DiskObject->do_CurrentX == (LONG)NO_ICON_POSITION) ? ~0 : my->DiskObject->do_CurrentX,
        GA_Top, (my->DiskObject->do_CurrentY == (LONG)NO_ICON_POSITION) ? ~0 : my->DiskObject->do_CurrentY,
        GA_Width, icon_w,
        GA_Height, icon_h,
        TAG_END);
}

// OM_NEW
static IPTR WBIcon__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my;
    CONST_STRPTR file, label = "???";

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)ops);
    if (obj == NULL)
        return 0;

    my = INST_DATA(cl, obj);

    my->Set = (Object *)GetTagData(WBIA_Set, (IPTR)NULL, ops->ops_AttrList);
    my->File = NULL;
    my->DiskObject = (struct DiskObject *)GetTagData(WBIA_Icon, (IPTR)NULL, ops->ops_AttrList);
    my->Screen = (struct Screen *)GetTagData(WBIA_Screen, (IPTR)NULL, ops->ops_AttrList);
    if (my->DiskObject != NULL) {
        if (my->DiskObject->do_Gadget.GadgetText != NULL &&
            my->DiskObject->do_Gadget.GadgetText->IText != NULL)
            label = my->DiskObject->do_Gadget.GadgetText->IText;
    } else {
        file = (CONST_STRPTR)GetTagData(WBIA_File, (IPTR)NULL, ops->ops_AttrList);
        if (file == NULL)
            goto error;

        my->File = StrDup(file);
        if (my->File == NULL)
            goto error;

        strcpy(my->File, file);

        label = FilePart(my->File);

        my->DiskObject = GetIconTags(my->File,
                               ICONGETA_Screen, my->Screen,
                               ICONGETA_FailIfUnavailable, FALSE,
                               TAG_END);
        if (my->DiskObject == NULL)
            goto error;

    }

    my->Label = StrDup((CONST_STRPTR)GetTagData(WBIA_Label, (IPTR)label, ops->ops_AttrList));
    if (my->Label == NULL)
        goto error;

    wbIcon_Update(cl, obj);

    return (IPTR)obj;

error:
    if (my->File)
        FreeVec(my->File);
    DoSuperMethod(cl, obj, OM_DISPOSE);
    return 0;
}

// OM_DISPOSE
static IPTR WBIcon__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
#if 0
    struct TagItem tags[] = {
        { ICONPUTA_OnlyUpdatePosition, TRUE },
        { TAG_END } };

    /* If need be, update the on-disk Icon's position information */
    PutIconTagList(my->File, my->DiskObject, tags);
#endif

    /* If my->File is set, then we allocated it
     * and my->DiskObject. Otherwise, Icon was passed in
     * via WBIA_Icon, and its the caller's responsibility.
     */
    if (my->File) {
        FreeVec(my->File);
        if (my->DiskObject)
            FreeDiskObject(my->DiskObject);
    }

    if (my->Label)
        FreeVec(my->Label);

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
    default:
        rc = DoSuperMethodA(cl, obj, (Msg)opg);
        break;
    }

    return rc;
}

static void wbGABox(Object *obj, struct IBox *box)
{
    struct Gadget *gadget = (struct Gadget *)obj;
    box->Top = gadget->TopEdge;
    box->Left = gadget->LeftEdge;
    box->Width = gadget->Width;
    box->Height = gadget->Height;
}

// OM_SET
static IPTR WBIcon__OM_SET(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    struct TagItem *tags = ops->ops_AttrList;
    struct TagItem *ti;

    BOOL render = FALSE;

    while ((ti = NextTagItem(&tags)) != NULL) {
        switch (ti->ti_Tag) {
        case GA_Selected:
            // Re-render if this attribute is present.
            render = TRUE;
            break;
        case WBIA_Set:
            my->Set = (Object *)ti->ti_Data;
            break;
        }
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
        DrawIconStateA(rp, my->DiskObject, (STRPTR)my->Label, x, y,
            (gadget->Flags & GFLG_SELECTED) ? IDS_SELECTED : IDS_NORMAL, (struct TagItem *)wbIcon_DrawTags);
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
static void wbIconToggleSelected(Class *cl, Object *obj, struct GadgetInfo *gi, UWORD qualifier)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    IPTR count = 0;

    // Toggle selection
    IPTR selected = FALSE;
    GetAttr(GA_Selected, obj, &selected);

    if (my->Set) {
        GetAttr(WBSA_SelectedCount, my->Set, &count);
    } else {
        count = selected ? 1 : 0;
    }

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
        D(bug("%s: %lx - clear parent set %lx\n", __func__, obj, my->Set));
        // Deselect ourself, so that we are not re-rendered.
        SetAttrs(obj, GA_Selected, FALSE, TAG_END);

        if (my->Set) {
            DoMethod(my->Set, WBSM_Select, gi, (IPTR)FALSE);
        }
    }

    SetAttrs(obj, GA_Selected, selected, TAG_END);

    // Redraw
    struct RastPort *rp;
    if ((rp = ObtainGIRPort(gi)) != NULL) {
        DoMethod(obj, GM_RENDER, gi, rp, GREDRAW_TOGGLE);
        ReleaseGIRPort(rp);
    }
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
    struct Gadget *gadget = (struct Gadget *)obj;
    BOOL dclicked = FALSE;
    IPTR rc = GMR_NOREUSE;

    if (gpi->gpi_IEvent != NULL) {
        my->LastActive = gpi->gpi_IEvent->ie_TimeStamp;

        // Select this item.
        wbIconToggleSelected(cl, obj, gpi->gpi_GInfo, gpi->gpi_IEvent->ie_Qualifier);
        rc = GMR_MEACTIVE;
    }

    return rc;
}

// GM_HANDLEINPUT
static IPTR WBIcon__GM_HANDLEINPUT(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct wbIcon *my = INST_DATA(cl, obj);
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct InputEvent *iev = gpi->gpi_IEvent;
    struct Gadget *gadget = (struct Gadget *)obj;

    IPTR rc = GMR_MEACTIVE;

    if (iev->ie_Class == IECLASS_RAWMOUSE) {
        switch (iev->ie_Code) {
        case SELECTDOWN:
            if ( (gpi->gpi_Mouse.X < 0) ||
                                 (gpi->gpi_Mouse.X > gadget->Width) ||
                                 (gpi->gpi_Mouse.Y < 0) ||
                                 (gpi->gpi_Mouse.Y > gadget->Height) ) {
                D(bug("%s: (%ld,%ld) clicked out of bounds (%ld,%ld)-(%ld,%ld)\n", __func__, 
                            gpi->gpi_Mouse.X, gpi->gpi_Mouse.Y,
                            0, 0, gadget->Width, gadget->Height
                            ));
                rc = GMR_REUSE;
            } else {

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
                    STACKED ULONG openmethodID;
                    openmethodID = WBIM_Open;
                    DoMethodA(obj, (Msg)&openmethodID);
                }

                wbIconToggleSelected(cl, obj, gpi->gpi_GInfo, gpi->gpi_IEvent->ie_Qualifier);
                rc = GMR_MEACTIVE;
            }
            break;
        case SELECTUP:
            rc = GMR_MEACTIVE;
            if ( (gpi->gpi_Mouse.X < 0) ||
                                 (gpi->gpi_Mouse.X > gadget->Width) ||
                                 (gpi->gpi_Mouse.Y < 0) ||
                                 (gpi->gpi_Mouse.Y > gadget->Height) ) {
                D(bug("%s: (%ld,%ld) clicked out of bounds (%ld,%ld)-(%ld,%ld)\n", __func__, 
                            gpi->gpi_Mouse.X, gpi->gpi_Mouse.Y,
                            0, 0, gadget->Width, gadget->Height
                            ));
                rc = GMR_REUSE;
            }
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

// WBIM_Open
static IPTR WBIcon__WBIM_Open(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    struct Process *proc;
    proc = CreateNewProcTags(
            NP_Name, (IPTR)my->File,
            NP_Seglist, (IPTR)wb->wb_OpenerSegList,
            NP_Arguments,   (IPTR)my->File,
            NP_FreeSeglist, (IPTR)FALSE,
            TAG_END);
    D(bug("WBIcon.Open: %s (0x%lx)\n", my->File, (IPTR)proc));

    return 0;
}

// WBIM_Copy
static IPTR WBIcon__WBIM_Copy(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

static BOOL rename_path(struct WorkbookBase *wb, CONST_STRPTR file, CONST_STRPTR input, CONST_STRPTR suffix) {
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

    ok = Rename(oldname, newname);
    FreeVec(oldname);

    return ok;
}

static IPTR rename_action(struct WorkbookBase *wb, CONST_STRPTR input, APTR arg){
    struct wbIcon *my = arg;
    CONST_STRPTR title = "Rename";
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
                    D(bug("%s: Can't relabel %s -> %s\n", buff, input));
                }
            }
        }
        title = "Relabel";
    } else {
        ok = rename_path(wb, my->File, input, "");
        if (ok) {
            ok = rename_path(wb, my->File, input, ".info");
        }
    }

    if (!ok) {
        wbPopupIoErr(wb, "Rename", IoErr(), my->File);
    }

    return ok;
}

// WBIM_Rename
static IPTR WBIcon__WBIM_Rename(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    ULONG len = STRLEN(my->File);
    IPTR ok = FALSE;

    if (len > 0 && my->File[len-1] == ':') {
        // Volume rename
        STRPTR name = AllocVec(FILENAME_MAX, MEMF_ANY);
        CopyMem(my->File, name, len);
        name[len - 1] = 0;
        ok = wbPopupAction(wb, "Relabel", "Enter a new volume name.", "New Name:", name, 0, ":/", rename_action, my);
        FreeVec(name);
    } else {
        ok = wbPopupAction(wb, "Rename", "Enter a new file name.", "New Name:", FilePart(my->File), 0, ":/", rename_action, my);
    }

    return ok ? WBIM_REFRESH : 0;
}

// WBIM_Info
static IPTR WBIcon__WBIM_Info(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    BPTR lock;

    lock = Lock(my->File, SHARED_LOCK);
    if (lock == BNULL) {
        wbPopupIoErr(wb, "Info", IoErr(), my->File);
    } else {
        // We'd like to use 'WBInfo', but the AROS ROMs directly call WANDERER:Tools/Info.
        // Sigh.
        WBInfo(lock, my->File, my->Screen);
        UnLock(lock);
    }

    return 0;
}

// WBIM_Snapshot
static IPTR WBIcon__WBIM_Snapshot(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    struct Gadget *gadget = (struct Gadget *)obj;

    D(bug("%s: %s\n", my->File));

    my->DiskObject->do_CurrentX = gadget->LeftEdge;
    my->DiskObject->do_CurrentY = gadget->TopEdge;
    PutIconTags(my->File, my->DiskObject, ICONPUTA_OnlyUpdatePosition, TRUE, TAG_END);

    return 0;
}

// WBIM_Unsnapshot
static IPTR WBIcon__WBIM_Unsnapshot(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    D(bug("%s: %s\n", my->File));

    my->DiskObject->do_CurrentX = (LONG)NO_ICON_POSITION;
    my->DiskObject->do_CurrentY = (LONG)NO_ICON_POSITION;
    PutIconTags(my->File, my->DiskObject, ICONPUTA_OnlyUpdatePosition, TRUE, TAG_END);
    return 0;
}

// WBIM_Leave_Out
static IPTR WBIcon__WBIM_Leave_Out(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Put_Away
static IPTR WBIcon__WBIM_Put_Away(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Delete
static IPTR WBIcon__WBIM_Delete(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBwbwiAppendIM_Format
static IPTR WBIcon__WBIM_Format(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    BPTR lock = Lock(my->File, ACCESS_READ);
    BOOL ok = FALSE;
    if (OpenWorkbenchObject("SYS:System/Format",
                WBOPENA_ArgLock, lock,
                WBOPENA_ArgName, (lock != BNULL) ? "" : my->File,
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
    return 0;
}

static IPTR WBIcon_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    _D(bug("WBIcon: dispatch 0x%lx\n", msg->MethodID));
    switch (msg->MethodID) {
    METHOD_CASE(WBIcon, OM_NEW);
    METHOD_CASE(WBIcon, OM_DISPOSE);
    METHOD_CASE(WBIcon, OM_GET);
    METHOD_CASE(WBIcon, OM_SET);
    METHOD_CASE(WBIcon, GM_RENDER);
    METHOD_CASE(WBIcon, GM_HITTEST);
    METHOD_CASE(WBIcon, GM_GOACTIVE);
    METHOD_CASE(WBIcon, GM_HANDLEINPUT);
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
