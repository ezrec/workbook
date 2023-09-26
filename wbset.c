/*
    Copyright (C) 2011, The AROS Development Team. All rights reserved.

    Desc: Workbook Icon Set Class
*/

#include <string.h>
#include <limits.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/utility.h>
#include <proto/gadtools.h>
#include <proto/graphics.h>

#include <intuition/classusr.h>
#include <libraries/gadtools.h>

#include "workbook_intern.h"
#include "classes.h"

#define WBICON_ROW_MARGIN   5
#define WBICON_COL_MARGIN   5

#ifndef SetDrPt
#define SetDrPt(w,p)	do { \
				(w)->LinePtrn = (p); \
				(w)->Flags |= FRST_DOT; \
				(w)->linpatcnt = 15; \
			} while (0)
#endif


struct wbSetNode {
    struct Node sn_Node;
    Object        *sn_Object;   // Gadget object
    BOOL           sn_Backdrop;    // Is the backdrop set?
    LONG           sn_CurrentX;    // do_CurrentX cache.
    LONG           sn_CurrentY;    // do_CurrentY cache.
};

#define IS_VISIBLE(node)    ((node)->sn_Backdrop == my->Backdrop)
#define IS_ARRANGED(node)   ((node)->sn_CurrentX != (LONG)NO_ICON_POSITION) && ((node)->sn_CurrentY != (LONG)NO_ICON_POSITION)

struct wbSet {
    struct List SetObjects;
    UWORD ViewModes;            // Same a 'DrawerData->dd_ViewModes'
    BOOL  Arranged;
    BOOL  Backdrop;
    struct Rectangle Marquee;
    BOOL MarqueeEnable;
};

static void wbGABox(Object *obj, struct IBox *box)
{
    struct Gadget *gadget = (struct Gadget *)obj;
    box->Top = gadget->TopEdge;
    box->Left = gadget->LeftEdge;
    box->Width = gadget->Width;
    box->Height = gadget->Height;
}

// OM_ADDMEMBER
static IPTR WBSet__OM_ADDMEMBER(Class *cl, Object *obj, struct opMember *opm)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    Object *iobj = opm->opam_Object;
    struct IBox ibox;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node;

    IPTR rc = 0;

    node = AllocVec(sizeof(*node), MEMF_ANY);
    if (node) {
        node->sn_Object = iobj;

        /* Get bounding box of item to add */
        wbGABox(iobj, &ibox);

        GetAttr(WBIA_Label, iobj, (IPTR *)&node->sn_Node.ln_Name);
        IPTR tmp;
        GetAttr(WBIA_Backdrop, iobj, &tmp);
        node->sn_Backdrop = (BOOL)tmp;
        GetAttr(WBIA_DoCurrentX, iobj, &tmp);
        node->sn_CurrentX = (LONG)tmp;
        GetAttr(WBIA_DoCurrentY, iobj, &tmp);
        node->sn_CurrentY = (LONG)tmp;
        AddTail(&my->SetObjects, &node->sn_Node);

        SetAttrs(iobj, WBIA_ListView, my->ViewModes != DDVM_BYICON, TAG_END);

        my->Arranged = FALSE;

        rc = DoSuperMethodA(cl, obj, (Msg)opm);
    }

    return rc;
}

static IPTR WBSet__OM_REMMEMBER(Class *cl, Object *obj, struct opMember *opm)
{
    Object *iobj = opm->opam_Object;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)opm);

    ForeachNodeSafe(&my->SetObjects, node, next) {
        if (node->sn_Object == iobj) {
            Remove(&node->sn_Node);
            FreeVec(node);
        }
    }

    my->Arranged = FALSE;

    return rc;
}

// OM_NEW
static IPTR WBSet__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct wbSet *my;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return 0;

    obj = (Object *)rc;
    my = INST_DATA(cl, obj);

    my->ViewModes = DDVM_BYICON;
    my->Arranged = FALSE;

    NEWLIST(&my->SetObjects);

    CoerceMethod(cl, obj, OM_SET, ops->ops_AttrList, NULL);

    return rc;
}

// OM_SET
static IPTR WBSet__OM_SET(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);

    struct TagItem *tags = ops->ops_AttrList;
    struct TagItem *ti;

    UWORD viewmodes = my->ViewModes;
    BOOL backdrop = my->Backdrop;

    while ((ti = NextTagItem(&tags)) != NULL) {
        switch (ti->ti_Tag) {
        case WBSA_ViewModes:
            viewmodes = (UWORD)ti->ti_Data;
            break;
        case WBSA_Backdrop:
            backdrop = (BOOL)ti->ti_Data;
        default:
            break;
        }
    }

    if (viewmodes > DDVM_BYDEFAULT && viewmodes <= DDVM_BYTYPE) {
        my->ViewModes = viewmodes;
        my->Arranged = FALSE;
    }

    if (backdrop != my->Backdrop) {
        my->Backdrop = backdrop;
    }

    return DoSuperMethodA(cl, obj, (Msg)ops);
}

// OM_DISPOSE
static IPTR WBSet__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;

    /* Remove all the nodes */
    ForeachNodeSafe(&my->SetObjects, node, next) {
        Remove(&node->sn_Node);
        FreeVec(node);
    }

    return DoSuperMethodA(cl, obj, msg);
}

// WBSM_Select
static IPTR WBSet__WBSM_Select(Class *cl, Object *obj, struct wbsm_Select *wbss)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;

    ForeachNodeSafe(&my->SetObjects, node, next) {
        IPTR selected = FALSE;
        GetAttr(GA_Selected, node->sn_Object, &selected);
        if (!!selected != !!wbss->wbss_All) {
            D(bug("%s: %lx - (de)select %lx\n", __func__, (IPTR)obj, (IPTR)node->sn_Object));
            SetAttrs(node->sn_Object, GA_Selected, !!wbss->wbss_All, TAG_END);
            DoMethod(node->sn_Object, GM_RENDER, wbss->wbss_GInfo, NULL, GREDRAW_TOGGLE);
        }
    }

    return 0;
}

static BOOL wbSetInsertByName(struct WorkbookBase *wb, struct wbSetNode *curr_node, struct wbSetNode *new_node)
{
    return stricmp(curr_node->sn_Node.ln_Name, new_node->sn_Node.ln_Name) < 0;
}

static BOOL wbSetInsertBySize(struct WorkbookBase *wb, struct wbSetNode *curr_node, struct wbSetNode *new_node)
{
    IPTR curr_size = 0;
    IPTR new_size = 0;
    GetAttr(WBIA_FibSize, curr_node->sn_Object, &curr_size );
    GetAttr(WBIA_FibSize, new_node->sn_Object, &new_size );
    LONG rc = (LONG)new_size - (LONG)curr_size;
    if (rc == 0) {
        return wbSetInsertByName(wb, curr_node, new_node);
    }
    return rc < 0;
}

static BOOL wbSetInsertByDate(struct WorkbookBase *wb, struct wbSetNode *curr_node, struct wbSetNode *new_node)
{
    struct DateStamp curr_date;
    struct DateStamp new_date;
    GetAttr(WBIA_FibDateStamp, curr_node->sn_Object, (IPTR *)&curr_date );
    GetAttr(WBIA_FibDateStamp, new_node->sn_Object, (IPTR *)&new_date );
    LONG rc = CompareDates(&curr_date, &new_date);
    if (rc == 0) {
        return wbSetInsertByName(wb, curr_node, new_node);
    }
    return rc < 0;
}

static BOOL wbSetInsertByType(struct WorkbookBase *wb, struct wbSetNode *curr_node, struct wbSetNode *new_node)
{
    IPTR curr_type = 0;
    IPTR new_type = 0;
    GetAttr(WBIA_DoType, curr_node->sn_Object, &curr_type );
    GetAttr(WBIA_DoType, new_node->sn_Object, &new_type );
    LONG rc = (LONG)new_type - (LONG)curr_type;
    if (rc == 0) {
        return wbSetInsertByName(wb, curr_node, new_node);
    }
    return rc < 0;
}

static void wbSetSort(Class *cl, Object *obj)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);

    struct List sorted;
    NEWLIST(&sorted);

    BOOL (*insertFunc)(struct WorkbookBase *wb, struct wbSetNode *curr_node, struct wbSetNode *new_node);

    switch (my->ViewModes) {
    case DDVM_BYICON: insertFunc = wbSetInsertByName; break;
    case DDVM_BYNAME: insertFunc = wbSetInsertByName; break;
    case DDVM_BYDATE: insertFunc = wbSetInsertByDate; break;
    case DDVM_BYSIZE: insertFunc = wbSetInsertBySize; break;
    case DDVM_BYTYPE: insertFunc = wbSetInsertByType; break;
    default: return; // Nothing to sort by.
    }

    struct wbSetNode *node, *next;
    D(bug("%s: Unsorted:\n", __func__));
    ForeachNode(&my->SetObjects, node) {
        D(bug("%s:  %s\n", __func__, node->sn_Node.ln_Name));
    }

    ForeachNodeSafe(&my->SetObjects, node, next) {
        Remove(&node->sn_Node);

        struct wbSetNode *curr;
        BOOL inserted = FALSE;
        ForeachNode(&sorted, curr) {
            if (insertFunc(wb, curr, node)) {
                D(bug("%s: insert %s before %s\n", __func__, node->sn_Node.ln_Name, curr->sn_Node.ln_Name));
                Insert(&sorted, &node->sn_Node, curr->sn_Node.ln_Pred);
                inserted = TRUE;
                break;
            }
        }
        if (!inserted) {
            AddTail(&sorted, &node->sn_Node);
            D(bug("%s: add tail: %s\n", __func__, node->sn_Node.ln_Name));
        }
    }

    // Move back to original list.
    ForeachNodeSafe(&sorted, node, next) {
        Remove(&node->sn_Node);
        AddTail(&my->SetObjects, &node->sn_Node);
    }
}

// WBSM_Clean_Up
static IPTR WBSet__WBSM_Clean_Up(Class *cl, Object *obj, struct wbsm_CleanUp *wbscu)
{
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;

    ForeachNodeSafe(&my->SetObjects, node, next) {
        node->sn_CurrentX = (LONG)NO_ICON_POSITION;
        node->sn_CurrentY = (LONG)NO_ICON_POSITION;
    }

    my->Arranged = FALSE;

    return 0;
}

static IPTR WBSet__GM_LAYOUT(Class *cl, Object *obj, struct gpLayout *gpl)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    struct GadgetInfo *gi = gpl->gpl_GInfo;
    struct IBox sbox;

    BOOL listView = my->ViewModes != DDVM_BYICON;

    // Re-arrange anything that needs to be.
    WORD CurrRight, CurrBottom;

    // Remove all members (and set their view mode)
    struct wbSetNode *node;
    ForeachNode(&my->SetObjects, node) {
        SetAttrs(node->sn_Object, WBIA_ListView, (IPTR)listView, TAG_END);
        DoSuperMethod(cl, obj, OM_REMMEMBER, node->sn_Object);
    }

    // Sort.
    wbSetSort(cl, obj);

    // If not listview, add fixed or arranged items first.
    if (!listView) {
        ForeachNode(&my->SetObjects, node) {
            if (IS_VISIBLE(node) && IS_ARRANGED(node)) {
                D(bug("%s: %s - fixed @%ld,%ld\n", __func__, node->sn_Node.ln_Name, (IPTR)node->sn_CurrentX, (IPTR)node->sn_CurrentY));
                SetAttrs(node->sn_Object, GA_Top, node->sn_CurrentY, GA_Left, node->sn_CurrentX, TAG_END);
                DoSuperMethod(cl, obj, OM_ADDMEMBER, node->sn_Object);
            }
        }
    }

    /* Find the set size with just the fixed and arranged objects */
    wbGABox(obj, &sbox);
    D(bug("%s: Prearrange box @(%ld,%ld) %ldx%ld\n", __func__, (IPTR)sbox.Left, (IPTR)sbox.Top, (IPTR)sbox.Width, (IPTR)sbox.Height));

    /* Set the start of the auto area to be
     * immediately below the current objects.
     */
    CurrRight = 0;
    CurrBottom = sbox.Height + (listView ? 0 : WBICON_ROW_MARGIN);

    /* For each item in the auto list, add it */
    ForeachNode(&my->SetObjects, node) {
        if (!IS_VISIBLE(node)) {
            D(bug("%s: %s - not visible\n", __func__, node->sn_Node.ln_Name));
            continue;
        }
        if (!listView && IS_ARRANGED(node)) {
            D(bug("%s: %s - fixed\n", __func__, node->sn_Node.ln_Name));
            continue;
        }

        D(bug("%s: %s - arrange\n", __func__, node->sn_Node.ln_Name));

        Object *iobj = node->sn_Object;
        struct IBox ibox;

        wbGABox(iobj, &ibox);
        D(bug("%s: icon is %ldx%lx\n", __func__, (IPTR)ibox.Width, (IPTR)ibox.Height));
        wbGABox(obj, &sbox);
        D(bug("%s: set  is %ldx%lx\n", __func__, (IPTR)ibox.Width, (IPTR)ibox.Height));

        if (!listView && ((CurrRight + ibox.Width) < gi->gi_Domain.Width)) {
            ibox.Left = CurrRight;
            D(bug("%s: %s add to right @(%ld,%ld)\n", __func__, node->sn_Node.ln_Name, (IPTR)CurrRight, (IPTR)CurrBottom));
        } else {
            ibox.Left = 0;
            CurrRight = 0;
            CurrBottom = sbox.Height + (listView ? 0 : WBICON_ROW_MARGIN);
            D(bug("%s: %s start new line @(%ld,%ld)\n", __func__, node->sn_Node.ln_Name, (IPTR)CurrRight, (IPTR)CurrBottom));
        }
        ibox.Top  = CurrBottom;
        CurrRight += ibox.Width + WBICON_COL_MARGIN;
        D(bug("%s: %s next: @%ld,%ld\n", __func__, node->sn_Node.ln_Name, (IPTR)CurrRight, (IPTR)CurrBottom));

        // Mark as arranged
        D(bug("%s: %s arranged position: @%ld,%ld\n", __func__, node->sn_Node.ln_Name, (IPTR)ibox.Left, (IPTR)ibox.Top));

        // Adjust gadget location _without_ re-rendering.
        SetAttrs(iobj, GA_Top, ibox.Top, GA_Left, ibox.Left, TAG_END);
        if (!listView) {
            // Update icon's DiskObject location.
            node->sn_CurrentX = ibox.Left;
            node->sn_CurrentY = ibox.Top;
            SetAttrs(iobj, WBIA_DoCurrentY, ibox.Top, WBIA_DoCurrentX, ibox.Left, TAG_END);
        }

        DoSuperMethod(cl, obj, OM_ADDMEMBER, iobj);
    }

    wbGABox(obj, &sbox);
    D(bug("%s: Arranged box @(%ld,%ld) %ldx%ld\n", __func__, (IPTR)sbox.Left, (IPTR)sbox.Top, (IPTR)sbox.Width, (IPTR)sbox.Height));

    my->Arranged = TRUE;

    return DoSuperMethodA(cl, obj, (Msg)gpl);
}

static IPTR WBSet__GM_RENDER(Class *cl, Object *obj, struct gpRender *gpr)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);

    if (!my->Arranged) {
        struct RastPort *rp = gpr->gpr_RPort;
        ASSERT(rp != NULL);

        struct IBox sbox;
        wbGABox(obj, &sbox);
        D(bug("%s: Erase box @(%ld,%ld) %ldx%ld\n", __func__, (IPTR)sbox.Left, (IPTR)sbox.Top, (IPTR)sbox.Width, (IPTR)sbox.Height));
        EraseRect(rp, sbox.Left, sbox.Top, sbox.Left+sbox.Width, sbox.Top+sbox.Height);

        CoerceMethod(cl, obj, GM_LAYOUT, gpr->gpr_GInfo, FALSE);
    }

    return DoSuperMethodA(cl, obj, (Msg)gpr);
}

static inline void wbDrawRect(struct WorkbookBase *wb, struct RastPort *rp, struct Rectangle *rect)
{
    Move(rp, rect->MinX, rect->MinY);
    Draw(rp, rect->MaxX, rect->MinY);
    Draw(rp, rect->MaxX, rect->MaxY);
    Draw(rp, rect->MinX, rect->MaxY);
    Draw(rp, rect->MinX, rect->MinY);
}

static void wbSetDrawMarquee(Class *cl, Object *obj, struct GadgetInfo *gi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    struct RastPort *rp = ObtainGIRPort(gi);

    struct Rectangle outer = {
        .MinX = my->Marquee.MinX - 1,
        .MinY = my->Marquee.MinY - 1,
        .MaxX = my->Marquee.MaxX + 1,
        .MaxY = my->Marquee.MaxY + 1,
    };

    if (rp) {
        ULONG mode = GetDrMd(rp);
        SetDrMd(rp, COMPLEMENT);
        SetDrPt(rp, 0x3333);
        wbDrawRect(wb, rp, &my->Marquee);
        wbDrawRect(wb, rp, &outer);
        SetDrPt(rp, 0xffff);
        SetDrMd(rp, mode);
        ReleaseGIRPort(rp);
    }
}

static IPTR WBSet__GM_HITTEST(Class *cl, Object *obj, struct gpHitTest *gpht)
{
    struct wbSet *my = INST_DATA(cl, obj);

    IPTR rc = DoSuperMethodA(cl, obj, (Msg)gpht);
    if (rc == 0) {
        D(bug("%s: HITTEST - in window, not icon\n", __func__));
        my->MarqueeEnable = TRUE;
        rc = GMR_GADGETHIT;
    } else {
        D(bug("%s: HITTEST - in icon\n", __func__));
    }

    return rc;
}

static IPTR WBSet__GM_GOACTIVE(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct wbSet *my = INST_DATA(cl, obj);

    if (!my->MarqueeEnable) {
        return DoSuperMethodA(cl, obj, (Msg)gpi);
    }

    my->Marquee.MinX = gpi->gpi_Mouse.X-1;
    my->Marquee.MinY = gpi->gpi_Mouse.Y-1;
    my->Marquee.MaxX = gpi->gpi_Mouse.X+1;
    my->Marquee.MaxY = gpi->gpi_Mouse.Y+1;

    // Draw the marquee
    wbSetDrawMarquee(cl, obj, gpi->gpi_GInfo);

    return GMR_MEACTIVE;
}

static IPTR WBSet__GM_HANDLEINPUT(Class *cl, Object *obj, struct gpInput *gpi)
{
    struct wbSet *my = INST_DATA(cl, obj);
    struct InputEvent *iev = gpi->gpi_IEvent;

    if (!my->MarqueeEnable) {
        return DoSuperMethodA(cl, obj, (Msg)gpi);
    }

    IPTR rc = GMR_MEACTIVE;

    // Erase the old the marquee
    wbSetDrawMarquee(cl, obj, gpi->gpi_GInfo);

    my->Marquee.MaxX = gpi->gpi_Mouse.X+1;
    my->Marquee.MaxY = gpi->gpi_Mouse.Y+1;

    // Draw the new marquee
    wbSetDrawMarquee(cl, obj, gpi->gpi_GInfo);

    if (iev->ie_Class == IECLASS_RAWMOUSE) {
        switch (iev->ie_Code) {
        case SELECTUP:
            rc = GMR_REUSE;
            break;
        default:
            rc = GMR_MEACTIVE;
            break;
        }
    }

    return rc;
}

static BOOL wbRectOverlap(struct Rectangle *a, struct Rectangle *b)
{
    // Swap min/max as needed
    if (a->MinX > a->MaxX) { WORD t = a->MinX; a->MinX = a->MaxX; a->MaxX = t; }
    if (a->MinY > a->MaxY) { WORD t = a->MinY; a->MinY = a->MaxY; a->MaxY = t; }
    if (b->MinX > b->MaxX) { WORD t = b->MinX; b->MinX = b->MaxX; b->MaxX = t; }
    if (b->MinY > b->MaxY) { WORD t = b->MinY; b->MinY = b->MaxY; b->MaxY = t; }

    // if rectangle has area 0, no overlap
    if (a->MinX == a->MaxX || a->MinY == a->MaxY) {
        D(bug("%s: a has no area\n", __func__));
        return FALSE;
    }
    if (b->MinX == b->MaxX || b->MinY == b->MaxY) {
        D(bug("%s: b has no area\n", __func__));
        return FALSE;
    }

    // If one rectangle is on left side of other
    if (a->MaxX < b->MinX || b->MaxX < a->MinX) {
        D(bug("%s: a and b do no overlay in X\n", __func__));
        return FALSE;
    }

    // If one rectangle is above other
    if (a->MaxY < b->MinY || b->MaxY < a->MinY) {
        D(bug("%s: a and b do no overlay in Y\n", __func__));
        return FALSE;
    }

    return TRUE;
}

static IPTR WBSet__GM_GOINACTIVE(Class *cl, Object *obj, struct gpGoInactive *gpgi)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);

    if (!my->MarqueeEnable) {
        return DoSuperMethodA(cl, obj, (Msg)gpgi);
    }

    D(bug("%s: marquee @%ld,%ld-%ld,%ld\n", __func__,
                (IPTR)my->Marquee.MinX,
                (IPTR)my->Marquee.MinY,
                (IPTR)my->Marquee.MaxX,
                (IPTR)my->Marquee.MaxY));

    // Clear the marquee
    wbSetDrawMarquee(cl, obj, gpgi->gpgi_GInfo);

    // Select all inside the marquee
    struct wbSetNode *node;
    ForeachNode(&my->SetObjects, node) {
        struct Rectangle hitbox;
        IPTR tmp;
        LONG top, left;
        GetAttr(GA_Left, node->sn_Object, &tmp); left = tmp;
        GetAttr(GA_Top, node->sn_Object, &tmp); top = tmp;
        GetAttr(WBIA_HitBox, node->sn_Object,(IPTR *)&hitbox);
        hitbox.MinX += left; hitbox.MaxX += left;
        hitbox.MinY += top ; hitbox.MaxY += top ;
        D(bug("%s: %s hitbox @%ld,%ld-%ld,%ld\n",__func__, node->sn_Node.ln_Name,
                (IPTR)hitbox.MinX,
                (IPTR)hitbox.MinY,
                (IPTR)hitbox.MaxX,
                (IPTR)hitbox.MaxY));
        if (wbRectOverlap(&my->Marquee, &hitbox)) {
            D(bug("%s: %s HIT\n", __func__, node->sn_Node.ln_Name));
            SetAttrs(node->sn_Object, GA_Selected, TRUE, TAG_END);
            struct RastPort *rp = ObtainGIRPort(gpgi->gpgi_GInfo);
            if (rp) {
                DoMethod(node->sn_Object, (IPTR)GM_RENDER, gpgi->gpgi_GInfo, rp, (IPTR)GREDRAW_TOGGLE);
                ReleaseGIRPort(rp);
            }
        }
    }

    my->MarqueeEnable = FALSE;

    return 0;
}

static IPTR WBSet__WBxM_DragDropped(Class *cl, Object *obj, struct wbxm_DragDropped *wbxmd)
{
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;
    struct Gadget *gadget = (struct Gadget *)obj;
    IPTR rc = FALSE;

    Object *target = NULL;
    ULONG gadgetX, gadgetY;

    ForeachNodeSafe(&my->SetObjects, node, next) {
        struct Gadget *subgad = (struct Gadget *)node->sn_Object;
        gadgetX = gadget->LeftEdge + wbxmd->wbxmd_MouseX - subgad->LeftEdge;
        gadgetY = gadget->TopEdge + wbxmd->wbxmd_MouseY - subgad->TopEdge;
        struct gpHitTest gpht = {
            .MethodID = GM_HITTEST,
            .gpht_GInfo = wbxmd->wbxmd_GInfo,
            .gpht_Mouse = {
                .X = gadgetX,
                .Y = gadgetY,
            },
        };
        if (DoMethodA(node->sn_Object, (Msg)&gpht)) {
            // Hit!
            target = node->sn_Object;
        }
    }

    if (target) {
        // Send the DragDrop to the target object
        rc = DoMethod(target, WBxM_DragDropped, NULL, gadgetX, gadgetY);
    }

    return rc;
}

static IPTR WBSet_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    METHOD_CASE(WBSet, OM_NEW);
    METHOD_CASE(WBSet, OM_SET);
    METHOD_CASE(WBSet, OM_DISPOSE);
    METHOD_CASE(WBSet, OM_ADDMEMBER);
    METHOD_CASE(WBSet, OM_REMMEMBER);
    METHOD_CASE(WBSet, GM_LAYOUT);
    METHOD_CASE(WBSet, GM_RENDER);
    METHOD_CASE(WBSet, GM_HITTEST);
    METHOD_CASE(WBSet, GM_GOACTIVE);
    METHOD_CASE(WBSet, GM_HANDLEINPUT);
    METHOD_CASE(WBSet, GM_GOINACTIVE);
    METHOD_CASE(WBSet, WBSM_Select);
    METHOD_CASE(WBSet, WBSM_Clean_Up);
    METHOD_CASE(WBSet, WBxM_DragDropped);
    default:            rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBSet_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "groupgclass", NULL,
                    sizeof(struct wbSet),
                    0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBSet_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
