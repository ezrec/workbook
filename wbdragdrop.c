/*
    Copyright (C) 2011-2020, The AROS Development Team. All rights reserved.

    Desc: Workbook Application Class
*/

#include <string.h>
#include <limits.h>

#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <intuition/gadgetclass.h>

#include "workbook_intern.h"
#include "classes.h"

struct wbDragDropImage {
    struct MinNode Node;
    ULONG IconType;
    APTR  IconImage;
    struct Rectangle IconRect;
};

struct wbDragDrop {
    struct Screen *Screen;      // Screen to render upon
    struct MinList ImageList;    // Rectangle list.

    BOOL Dragging;
    ULONG CurrentX;
    ULONG CurrentY;
    ULONG OriginX;
    ULONG OriginY;
};

// COMPLEMENT (XOR) draw all the imagery. Draw it twice - and the original bitmap is restored!
static void wbDragDropDraw(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbDragDrop *my = INST_DATA(cl, obj);

    struct RastPort *rp = &my->Screen->RastPort;

    D(bug("%s: o(%ld,%ld), mouse(%ld,%ld)\n", __func__, (IPTR)my->OriginX, (IPTR)my->OriginY, (IPTR)my->CurrentX, (IPTR)my->CurrentY));
    LONG deltaX = (LONG)my->CurrentX - (LONG)my->OriginX;
    LONG deltaY = (LONG)my->CurrentY - (LONG)my->OriginY;

    struct wbDragDropImage *image;

    ULONG mode = GetDrMd(rp);
    SetDrMd(rp, COMPLEMENT);
    ForeachNode(&my->ImageList, image) {
        struct Rectangle *rect = (struct Rectangle *)&image->IconRect;

        switch (image->IconType) {
        case WBDT_IMAGE:
            DrawImage(rp, image->IconImage, deltaX, deltaY);
            break;
        case WBDT_RECTANGLE:
            D(bug("%s: RectFill(%lx, (%ld,%ld)-(%ld,%ld)\n", __func__, (IPTR)rp, (IPTR)(deltaX + rect->MinX), (IPTR)(deltaY + rect->MinY), (IPTR)(deltaX + rect->MaxX), (IPTR)(deltaY + rect->MaxY)));
            RectFill(rp, deltaX + rect->MinX, deltaY + rect->MinY, deltaX + rect->MaxX, deltaY + rect->MaxY);
            break;
        default:
            break;
        }
    }
    SetDrMd(rp, mode);
}

static IPTR WBDragDrop__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbDragDrop *my = NULL;
    IPTR rc;

    // A screen is required!
    struct Screen *screen = (struct Screen *)GetTagData(WBDA_Screen, (IPTR)NULL, ops->ops_AttrList);
    if (screen == NULL) {
        return 0;
    }

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0) {
        return 0;
    }

    my = INST_DATA(cl, rc);

    my->Screen = screen;

    NEWLIST(&my->ImageList);

    // Set any additional attributes
    CoerceMethod(cl, obj, OM_SET, ops->ops_AttrList, ops->ops_GInfo);

    return rc;
}

static IPTR WBDragDrop__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    CoerceMethod(cl, obj, WBDM_Clear);

    return DoSuperMethodA(cl, obj, msg);
}

static inline WORD ABS(WORD n) { return n < 0 ? -n : n; }

static IPTR WBDragDrop__WBDM_Update(Class *cl, Object *obj, Msg msg)
{
    struct wbDragDrop *my = INST_DATA(cl, obj);

    D(bug("%s: Dragging: was %s\n", __func__, my->Dragging ? "TRUE" : "false"));

    // No longer active?
    if (!my->Dragging) {
        my->CurrentX = my->Screen->MouseX;
        my->CurrentY = my->Screen->MouseY;

        if (ABS(my->CurrentX - my->OriginX) > 5 && ABS(my->CurrentY - my->OriginY) > 5) {
            // Draw the initial XOR imagery
            wbDragDropDraw(cl, obj);

            my->Dragging = TRUE;
        }
    } else {
        // Erase the imagery
        wbDragDropDraw(cl, obj);

        my->CurrentX = my->Screen->MouseX;
        my->CurrentY = my->Screen->MouseY;

        // Draw the imagery
        wbDragDropDraw(cl, obj);
    }

    D(bug("%s: Dragging: is %s\n", __func__, my->Dragging ? "TRUE" : "false"));

    return my->Dragging;
}

static IPTR WBDragDrop__WBDM_Begin(Class *cl, Object *obj, Msg msg)
{
    struct wbDragDrop *my = INST_DATA(cl, obj);

    // Use the current mouse location as the origin.
    my->OriginX = my->Screen->MouseX;
    my->OriginY = my->Screen->MouseY;

    return 0;
}

static IPTR WBDragDrop__WBDM_End(Class *cl, Object *obj, Msg msg)
{
    struct wbDragDrop *my = INST_DATA(cl, obj);

    BOOL dragged = my->Dragging;

    if (dragged) {
        my->Dragging = FALSE;

        // Erase the final XOR imagery
        wbDragDropDraw(cl, obj);
    }

    return dragged;
}

static IPTR WBDragDrop__WBDM_Add(Class *cl, Object *obj, struct wbdm_Add *wbdma)
{
    struct wbDragDrop *my = INST_DATA(cl, obj);
    struct wbDragDropImage *node;

    node = AllocMem(sizeof(*node), MEMF_ANY | MEMF_CLEAR);
    if (!node) {
        return FALSE;
    }

    node->IconType = wbdma->wbdma_ImageType;
    switch (node->IconType) {
    case WBDT_IMAGE:
        node->IconImage = wbdma->wbdma_ImageData;
        break;
    case WBDT_RECTANGLE:
        node->IconRect = *(struct Rectangle *)wbdma->wbdma_ImageData;
        D(bug("%s: Rect: (%ld,%ld)-(%ld,%ld)\n", __func__, (IPTR)node->IconRect.MinX, (IPTR)node->IconRect.MinY, (IPTR)node->IconRect.MaxX, (IPTR)node->IconRect.MaxY));
        break;
    default:
        FreeMem(node, sizeof(*node));
        return FALSE;
    }

    AddTailMinList(&my->ImageList, &node->Node);

    return TRUE;
}

static IPTR WBDragDrop__WBDM_Clear(Class *cl, Object *obj, Msg msg)
{
    struct wbDragDrop *my = INST_DATA(cl, obj);
    struct wbDragDropImage *node, *next;

    ForeachNodeSafe(&my->ImageList, node, next) {
        RemoveMinNode(&node->Node);
        FreeMem(node, sizeof(*node));
    }

    return 0;
}

static IPTR WBDragDrop_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    D(bug("%s: MessageID 0x%lx\n", __func__, (IPTR)msg->MethodID));
    switch (msg->MethodID) {
    METHOD_CASE(WBDragDrop, OM_NEW);
    METHOD_CASE(WBDragDrop, OM_DISPOSE);
    METHOD_CASE(WBDragDrop, WBDM_Begin);
    METHOD_CASE(WBDragDrop, WBDM_Update);
    METHOD_CASE(WBDragDrop, WBDM_End);
    METHOD_CASE(WBDragDrop, WBDM_Add);
    METHOD_CASE(WBDragDrop, WBDM_Clear);
    default:               rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBDragDrop_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "rootclass", NULL, sizeof(struct wbDragDrop), 0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBDragDrop_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
