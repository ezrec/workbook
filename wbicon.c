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
    struct DiskObject *Icon;
    STRPTR             Label;
    struct Screen     *Screen;

    struct timeval LastActive;
};

static const struct TagItem wbIcon_DrawTags[] = {
    { ICONDRAWA_Frameless, TRUE, },
    { ICONDRAWA_Borderless, TRUE, },
    { ICONDRAWA_EraseBackground, FALSE, },
    { TAG_DONE },
};

#ifndef __amigaos4__
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
    UWORD w,h;

    /* Update the parent's idea of how big we are
     */
    GetIconRectangleA(&my->Screen->RastPort, my->Icon, (STRPTR)my->Label, &rect, (struct TagItem *)wbIcon_DrawTags);

    w = (rect.MaxX - rect.MinX) + 1;
    h = (rect.MaxY - rect.MinY) + 1;

    /* If the icon is outside of the bounds for this
     * screen, ignore the position information
     */
    if ((my->Icon->do_CurrentX != (LONG)NO_ICON_POSITION ||
         my->Icon->do_CurrentY != (LONG)NO_ICON_POSITION) && my->Screen) {
        if ((my->Icon->do_CurrentX != (LONG)NO_ICON_POSITION &&
            (my->Icon->do_CurrentX < my->Screen->LeftEdge ||
            (my->Icon->do_CurrentX > (my->Screen->LeftEdge + my->Screen->Width - w)))) ||
            (my->Icon->do_CurrentY != (LONG)NO_ICON_POSITION &&
            (my->Icon->do_CurrentY < my->Screen->TopEdge ||
            (my->Icon->do_CurrentY > (my->Screen->TopEdge + my->Screen->Height - h))))) {
            my->Icon->do_CurrentY = (LONG)NO_ICON_POSITION;
            my->Icon->do_CurrentX = (LONG)NO_ICON_POSITION;
        }
    }

    D(bug("%s: %ldx%ld @%ld,%ld (%s)\n", my->File, w, h, my->Icon->do_CurrentX, my->Icon->do_CurrentY, my->Label));
    SetAttrs(obj,
        GA_Left, (my->Icon->do_CurrentX == (LONG)NO_ICON_POSITION) ? ~0 : my->Icon->do_CurrentX,
        GA_Top, (my->Icon->do_CurrentY == (LONG)NO_ICON_POSITION) ? ~0 : my->Icon->do_CurrentY,
        GA_Width, w,
        GA_Height, h,
        TAG_END);
}

// OM_NEW
static IPTR wbIconNew(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my;
    CONST_STRPTR file, label = "???";

    obj = (Object *)DoSuperMethodA(cl, obj, (Msg)ops);
    if (obj == NULL)
        return 0;

    my = INST_DATA(cl, obj);

    my->File = NULL;
    my->Icon = (struct DiskObject *)GetTagData(WBIA_Icon, (IPTR)NULL, ops->ops_AttrList);
    my->Screen = (struct Screen *)GetTagData(WBIA_Screen, (IPTR)NULL, ops->ops_AttrList);
    if (my->Icon != NULL) {
        if (my->Icon->do_Gadget.GadgetText != NULL &&
            my->Icon->do_Gadget.GadgetText->IText != NULL)
            label = my->Icon->do_Gadget.GadgetText->IText;
    } else {
        file = (CONST_STRPTR)GetTagData(WBIA_File, (IPTR)NULL, ops->ops_AttrList);
        if (file == NULL)
            goto error;

        my->File = StrDup(file);
        if (my->File == NULL)
            goto error;

        strcpy(my->File, file);

        label = FilePart(my->File);

        my->Icon = GetIconTags(my->File,
                               ICONGETA_Screen, my->Screen,
                               ICONGETA_FailIfUnavailable, FALSE,
                               TAG_END);
        if (my->Icon == NULL)
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
static IPTR wbIconDispose(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
#if 0
    struct TagItem tags[] = {
        { ICONPUTA_OnlyUpdatePosition, TRUE },
        { TAG_END } };

    /* If need be, update the on-disk Icon's position information */
    PutIconTagList(my->File, my->Icon, tags);
#endif

    /* If my->File is set, then we allocated it
     * and my->Icon. Otherwise, Icon was passed in
     * via WBIA_Icon, and its the caller's responsibility.
     */
    if (my->File) {
        FreeVec(my->File);
        if (my->Icon)
            FreeDiskObject(my->Icon);
    }

    if (my->Label)
        FreeVec(my->Label);

    return DoSuperMethodA(cl, obj, msg);
}

// OM_GET
static IPTR wbIconGet(Class *cl, Object *obj, struct opGet *opg)
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

// GM_RENDER
static IPTR wbIconRender(Class *cl, Object *obj, struct gpRender *gpr)
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

    /* Clip to the window for drawing */
    clip = wbClipWindow(wb, win);
    DrawIconStateA(rp, my->Icon, (STRPTR)my->Label, x, y,
        (gadget->Flags & GFLG_SELECTED) ? TRUE : FALSE, (struct TagItem *)wbIcon_DrawTags);
    wbUnclipWindow(wb, win, clip);

    return 0;
}

// GM_GOACTIVE
static IPTR wbIconGoActive(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);
    struct Gadget *gadget = (struct Gadget *)obj;
    BOOL dclicked;
    struct Region *clip;
    struct RastPort *rp;

    if ((rp = ObtainGIRPort(gpi->gpi_GInfo))) {
        struct gpRender rmsg;
        rmsg.MethodID = GM_RENDER;
        rmsg.gpr_GInfo = gpi->gpi_GInfo;
        rmsg.gpr_RPort = rp;
        rmsg.gpr_Redraw = GREDRAW_TOGGLE;

        gadget->Flags ^= GFLG_SELECTED;
        /* Clip to the window for drawing */
        clip = wbClipWindow(wb, gpi->gpi_GInfo->gi_Window);
        DoMethodA(obj, (Msg)&rmsg);
        wbUnclipWindow(wb, gpi->gpi_GInfo->gi_Window, clip);
        ReleaseGIRPort(rp);
    }

    /* On a double-click, don't go 'active', just
     * do the action.
     */
    dclicked = DoubleClick(my->LastActive.tv_secs,
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

    return GMR_MEACTIVE;
}

// GM_GOINACTIVE
static IPTR wbIconGoInactive(Class *cl, Object *obj, struct gpGoInactive *gpgi)
{
    return GMR_NOREUSE;
}

// GM_HANDLEINPUT
static IPTR wbIconHandleInput(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct InputEvent *iev = gpi->gpi_IEvent;

    if ((iev->ie_Class == IECLASS_RAWMOUSE) &&
        (iev->ie_Code  == SELECTUP))
        return GMR_REUSE;

    return GMR_MEACTIVE;
}

// WBIM_Open
static IPTR wbIconOpen(Class *cl, Object *obj, Msg msg)
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
static IPTR wbIconCopy(Class *cl, Object *obj, Msg msg)
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
        struct EasyStruct es = {
           .es_StructSize = sizeof(es),
           .es_Flags = 0,
           .es_Title = (STRPTR)title,
           .es_TextFormat = "%s",
           .es_GadgetFormat = "Ok",
        };
        char buff[80];
        Fault(IoErr(), my->File, buff, sizeof(buff));
        EasyRequest(0, &es, 0, buff, input);
    }

    return ok;
}

// WBIM_Rename
static IPTR wbIconRename(Class *cl, Object *obj, Msg msg)
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
static IPTR wbIconInfo(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbIcon *my = INST_DATA(cl, obj);

    WBInfo(BNULL, my->File, NULL);

    return 0;
}

// WBIM_Snapshot
static IPTR wbIconSnapshot(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Unsnapshot
static IPTR wbIconUnsnapshot(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Leave_Out
static IPTR wbIconLeaveOut(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Put_Away
static IPTR wbIconPutAway(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Delete
static IPTR wbIconDelete(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Format
static IPTR wbIconFormat(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

// WBIM_Empty_Trash
static IPTR wbIconEmptyTrash(Class *cl, Object *obj, Msg msg)
{
    return 0;
}

static IPTR dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    _D(bug("WBIcon: dispatch 0x%lx\n", msg->MethodID));
    switch (msg->MethodID) {
    case OM_NEW:           rc = wbIconNew(cl, obj, (APTR)msg); break;
    case OM_DISPOSE:       rc = wbIconDispose(cl, obj, (APTR)msg); break;
    case OM_GET:           rc = wbIconGet(cl, obj, (APTR)msg); break;
    case GM_RENDER:        rc = wbIconRender(cl, obj, (APTR)msg); break;
    case GM_GOACTIVE:      rc = wbIconGoActive(cl, obj, (APTR)msg); break;
    case GM_GOINACTIVE:    rc = wbIconGoInactive(cl, obj, (APTR)msg); break;
    case GM_HANDLEINPUT:   rc = wbIconHandleInput(cl, obj, (APTR)msg); break;
    case WBIM_Open:        rc = wbIconOpen(cl, obj, msg); break;
    case WBIM_Copy:        rc = wbIconCopy(cl, obj, msg); break;
    case WBIM_Rename:      rc = wbIconRename(cl, obj, msg); break;
    case WBIM_Info:        rc = wbIconInfo(cl, obj, msg); break;
    case WBIM_Snapshot:    rc = wbIconSnapshot(cl, obj, msg); break;
    case WBIM_Unsnapshot:  rc = wbIconUnsnapshot(cl, obj, msg); break;
    case WBIM_Leave_Out:   rc = wbIconLeaveOut(cl, obj, msg); break;
    case WBIM_Put_Away:    rc = wbIconPutAway(cl, obj, msg); break;
    case WBIM_Delete:      rc = wbIconDelete(cl, obj, msg); break;
    case WBIM_Format:      rc = wbIconFormat(cl, obj, msg); break;
    case WBIM_Empty_Trash: rc = wbIconEmptyTrash(cl, obj, msg); break;
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
        cl->cl_Dispatcher.h_SubEntry = dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
