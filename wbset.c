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

struct wbSetNode {
    struct MinNode sn_Node;
    Object        *sn_Object;   // Gadget object
    BOOL sn_Fixed;              // Is in a fixed location
    BOOL sn_Arranged;           // Has been arranged?
};

struct wbSet {
    ULONG MemberCount;
    struct MinList SetObjects;
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

    node = AllocMem(sizeof(*node), MEMF_ANY);
    node->sn_Object = iobj;

    /* Get bounding box of item to add */
    wbGABox(iobj, &ibox);

    node->sn_Arranged = FALSE;
    node->sn_Fixed = (ibox.Left != ~0) && (ibox.Top != ~0 );
    AddTailMinList(&my->SetObjects, &node->sn_Node);

    my->MemberCount++;

    return DoSuperMethodA(cl, obj, (Msg)opm);
}

static IPTR WBSet__OM_REMMEMBER(Class *cl, Object *obj, struct opMember *opm)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    Object *iobj = opm->opam_Object;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)opm);

    ForeachNodeSafe(&my->SetObjects, node, next) {
        if (node->sn_Object == iobj) {
            RemoveMinNode(&node->sn_Node);
            FreeMem(node, sizeof(*node));
        }
    }

    my->MemberCount--;

    return rc;
}

// OM_NEW
static IPTR WBSet__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my;
    IPTR rc;

    rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0)
        return 0;

    my = INST_DATA(cl, rc);

    my->MemberCount = 0;

    NewMinList(&my->SetObjects);

    return rc;
}

static ULONG wbSetSelectedCount(struct WorkbookBase *wb, struct wbSet *my)
{
    struct wbSetNode *node, *next;
    ULONG count = 0;

    ForeachNodeSafe(&my->SetObjects, node, next) {
        IPTR selected = FALSE;
        GetAttr(GA_Selected, node->sn_Object, &selected);
        if (selected) {
            count++;
        }
    }

    return count;
 }

static IPTR WBSet__OM_GET(Class *cl, Object *obj, struct opGet *opg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    IPTR rc = TRUE;

    switch (opg->opg_AttrID) {
    case WBSA_MemberCount:
        *(opg->opg_Storage) = (IPTR)my->MemberCount;
        break;
    case WBSA_SelectedCount:
        *(opg->opg_Storage) = (IPTR)wbSetSelectedCount(wb, my);
        break;
    default:
        rc = DoSuperMethodA(cl, obj, (Msg)opg);
        break;
    }

    return rc;
}

// OM_DISPOSE
static IPTR WBSet__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;

    /* Remove all the nodes */
    ForeachNodeSafe(&my->SetObjects, node, next) {
        RemoveMinNode(&node->sn_Node);
        FreeMem(node, sizeof(*node));
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
            D(bug("%s: %lx - (de)select %lx\n", __func__, obj, node->sn_Object));
            SetAttrs(node->sn_Object, GA_Selected, !!wbss->wbss_All, TAG_END);
            DoMethod(node->sn_Object, GM_RENDER, wbss->wbss_GInfo, NULL, GREDRAW_TOGGLE);
        }
    }

    return 0;
}

// WBSM_Clean_Up
static IPTR WBSet__WBSM_Clean_Up(Class *cl, Object *obj, struct wbsm_CleanUp *wbscu)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node, *next;

    ForeachNodeSafe(&my->SetObjects, node, next) {
        node->sn_Fixed = FALSE;
        node->sn_Arranged = FALSE;
    }

    return DoMethod(obj, GM_RENDER, wbscu->wbscu_GInfo, NULL, (IPTR)GREDRAW_REDRAW);
}

static IPTR WBSet__GM_RENDER(Class *cl, Object *obj, struct gpRender *gpr)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbSet *my = INST_DATA(cl, obj);
    struct wbSetNode *node;
    struct RastPort *rp = gpr->gpr_RPort;
    struct IBox sbox;   // Surrounding box, before rearrangement.

    if (!rp) {
        rp = ObtainGIRPort(gpr->gpr_GInfo);
    }

    struct Region *clip = wbClipWindow(wb, gpr->gpr_GInfo->gi_Window);

    // Erase surrounding box.
    wbGABox(obj, &sbox);
    D(bug("%s: Erase box @(%ld,%ld) %ldx%ld\n", __func__, sbox.Left, sbox.Top, sbox.Width, sbox.Height));
    EraseRect(rp, sbox.Left, sbox.Top, sbox.Left+sbox.Width, sbox.Top+sbox.Height);

    // Re-arrange anything that needs to be.
    WORD CurrRight, CurrBottom;
    /* First, remove all autoobjects from the superclass */
    BOOL render = FALSE;
    struct MinList floating;

    // Remove members that are not fixed nor arranged.
    ForeachNode(&my->SetObjects, node) {
        if (!node->sn_Fixed && !node->sn_Arranged) {
            D(bug("%s: %lx - Fixed=FALSE and Arranged=FALSE\n", __func__, node));
            DoSuperMethod(cl, obj, OM_REMMEMBER, node->sn_Object);
        }
    }

    /* Find the set size with just the fixed and arranged objects */
    wbGABox(obj, &sbox);
    D(bug("%s: Prearrange box @(%ld,%ld) %ldx%ld\n", __func__, sbox.Left, sbox.Top, sbox.Width, sbox.Height));

    /* Set the start of the auto area to be
     * immediately below the current objects.
     */
    CurrRight = sbox.Left;
    CurrBottom = sbox.Top + sbox.Height;

    /* For each item in the auto list, add it to the right */
    ForeachNode(&my->SetObjects, node) {
        if (node->sn_Fixed || node->sn_Arranged) {
            continue;
        }

        Object *iobj = node->sn_Object;
        struct IBox ibox;

        wbGABox(iobj, &ibox);

        if ((CurrRight + ibox.Width) < gpr->gpr_GInfo->gi_Domain.Width) {
            ibox.Left = CurrRight;
            D(bug("%s: %lx add to right @(%ld,%ld)\n", __func__, node, CurrRight, CurrBottom));
        } else {
            wbGABox(obj, &sbox);
            ibox.Left = sbox.Left;
            CurrRight = sbox.Left;
            CurrBottom = sbox.Top + sbox.Height + WBICON_ROW_MARGIN;
            D(bug("%s: %lx start new line @(%ld,%ld)\n", __func__, node, CurrRight, CurrBottom));
        }
        ibox.Top  = CurrBottom;
        CurrRight += ibox.Width + WBICON_COL_MARGIN;
        D(bug("%s: %lx next: @%ld,%ld\n", __func__, node, CurrRight, CurrBottom));

        // Mark as arranged
        D(bug("%s: %lx arranged position: @%ld,%ld\n", __func__, node, ibox.Left, ibox.Top));

        // Adjust gadget location _without_ re-rendering.
        SetAttrs(iobj, GA_Top, ibox.Top, GA_Left, ibox.Left, TAG_END);

        DoSuperMethod(cl, obj, OM_ADDMEMBER, iobj);

        node->sn_Arranged = TRUE;
    }

    wbGABox(obj, &sbox);
    D(bug("%s: Arranged box @(%ld,%ld) %ldx%ld\n", __func__, sbox.Left, sbox.Top, sbox.Width, sbox.Height));

    // Call supermethod to render the new arrangement.
    IPTR rc = DoSuperMethod(cl, obj, GM_RENDER, gpr->gpr_GInfo, rp, GREDRAW_UPDATE);

    wbUnclipWindow(wb, gpr->gpr_GInfo->gi_Window, clip);

    if (gpr->gpr_RPort == NULL) {
        ReleaseGIRPort(rp);
    }

    return rc;
}



static IPTR WBSet_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    if (msg->MethodID != GM_HANDLEINPUT) {
        D(bug("WBSet: dispatch 0x%lx\n", msg->MethodID));
    }

    switch (msg->MethodID) {
    METHOD_CASE(WBSet, OM_NEW);
    METHOD_CASE(WBSet, OM_DISPOSE);
    METHOD_CASE(WBSet, OM_GET);
    METHOD_CASE(WBSet, OM_ADDMEMBER);
    METHOD_CASE(WBSet, OM_REMMEMBER);
    METHOD_CASE(WBSet, GM_RENDER);
    METHOD_CASE(WBSet, WBSM_Select);
    METHOD_CASE(WBSet, WBSM_Clean_Up);
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
