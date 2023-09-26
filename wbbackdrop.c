// Copyright 2023, Jason S. McMullan <jason.mcmullan@gmail.com>
//
// This code licensed under the MIT License, as follows:
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the “Software”), to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions
// of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
// TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include <string.h>
#include <limits.h>

#include <proto/exec.h>
#include <proto/utility.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <intuition/gadgetclass.h>

#include "workbook_intern.h"
#include "classes.h"
#include "wbcurrent.h"

struct wbBackdropVolume {
    struct Node bv_Node;
    BPTR bv_Lock;
    struct List bv_Backdrops;
};

struct wbBackdrop {
    struct List Volumes;
};

static IPTR WBBackdrop__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;

    IPTR rc = DoSuperMethodA(cl, obj, (Msg)ops);
    if (rc == 0) {
        return 0;
    }

    obj = (Object *)rc;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    NEWLIST(&my->Volumes);

    return rc;
}

static IPTR WBBackdrop__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    struct wbBackdropVolume *node;
    while ((node = (struct wbBackdropVolume *)RemHead(&my->Volumes)) != NULL) {
        Remove(&node->bv_Node);
        wbBackdropFree(&node->bv_Backdrops);
        UnLock(node->bv_Lock);
        FreeVec(node);
    }

    return DoSuperMethodA(cl, obj, msg);
}

static IPTR WBBackdrop__WBBM_LockIs(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;
    if (wlock == BNULL) {
        return FALSE;
    }

    BOOL ok = FALSE;
    struct wbBackdropVolume *node;
    ForeachNode(&my->Volumes, node) {
        ok = wbBackdropContains(&node->bv_Backdrops, wlock);
        if (ok) {
            break;
        }
    }

    return ok;
}

static IPTR WBBackdrop__WBBM_LockNext(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;

    struct wbBackdropVolume *node;
    BPTR next = BNULL;
    ForeachNode(&my->Volumes, node) {
        BOOL present = wbBackdropNext(&node->bv_Backdrops, wlock, &next);
        if (next != BNULL) {
            break;
        }
        if (wlock != BNULL && present) {
            // It was at the end of this list. Get the first of the following list.
            wlock = BNULL;
        }
    }

    return next;
}


static IPTR WBBackdrop__WBBM_LockAdd(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;
    if (wlock == BNULL) {
        return FALSE;
    }

    BOOL ok = FALSE;
    struct wbBackdropVolume *node;
    D(bug("%s: Add lock(%s) to it's volume backdrop.\n", __func__, sLOCKNAME(wlock)));
    ForeachNode(&my->Volumes, node) {
        D(bug("%s: Inspect volume: %s\n", __func__, sLOCKNAME(node->bv_Lock)));
        if (SameDevice(node->bv_Lock, wlock)) {
            // Same volume
            D(bug("%s: New lock on the same volume\n", __func__));
            ok = wbBackdropAdd(&node->bv_Backdrops, wlock);
            D(if (!ok) bug("%s: Unable to add lock to volume(%s) .backdrop\n", __func__, sLOCKNAME(node->bv_Lock)));
            if (ok) {
                BPTR pwd = CurrentDir(node->bv_Lock);
                ok = wbBackdropSaveCurrent(&node->bv_Backdrops);
                D(if (ok) bug("%s: Saved volume(%s) .backdrop\n", __func__, sLOCKNAME(node->bv_Lock)));
                D(if (!ok) bug("%s: Unable to save volume(%s) .backdrop\n", __func__, sLOCKNAME(node->bv_Lock)));
                CurrentDir(pwd);
            }
            break;
        }
    }

    return ok;
}

static IPTR WBBackdrop__WBBM_LockDel(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;
    if (wlock == BNULL) {
        return FALSE;
    }

    BOOL ok = FALSE;
    struct wbBackdropVolume *node;
    D(bug("%s: Del lock(%s) from it's volume backdrop.\n", __func__, sLOCKNAME(wlock)));
    ForeachNode(&my->Volumes, node) {
        D(bug("%s: Inspect volume: %s\n", __func__, sLOCKNAME(node->bv_Lock)));
        if (SameDevice(node->bv_Lock, wlock)) {
            // Same volume.
            ok = wbBackdropDel(&node->bv_Backdrops, wlock);
            if (ok) {
                BPTR pwd = CurrentDir(node->bv_Lock);
                wbBackdropSaveCurrent(&node->bv_Backdrops);
                CurrentDir(pwd);
            }
            break;
        }
    }

    return ok;
}

static IPTR WBBackdrop__WBBM_VolumeAdd(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;
    if (wlock == BNULL) {
        return FALSE;
    }

    BOOL ok = FALSE;
    struct wbBackdropVolume *node;
    ForeachNode(&my->Volumes, node) {
        if (SameLock(node->bv_Lock, wlock) == LOCK_SAME) {
            // Already in the volume list.
            return TRUE;
        }
    }

    node = AllocVec(sizeof(struct wbBackdropVolume), MEMF_ANY | MEMF_CLEAR);
    if (node) {
        BPTR lock = DupLock(wlock);
        if (lock) {
            BPTR pwd = CurrentDir(lock);
            wbBackdropLoadCurrent(&node->bv_Backdrops);
            CurrentDir(pwd);

            node->bv_Lock = lock;
            AddTail(&my->Volumes, &node->bv_Node);

            ok = TRUE;
        } else {
            FreeVec(node);
        }
    }

    return ok;
}

static IPTR WBBackdrop__WBBM_VolumeDel(Class *cl, Object *obj, struct wbbm_Lock *wbbml)
{
    struct WorkbookBase *wb = (APTR)cl->cl_UserData;
    struct wbBackdrop *my = INST_DATA(cl, obj);

    BPTR wlock = wbbml->wbbml_Lock;
    if (wlock == BNULL) {
        return FALSE;
    }

    struct wbBackdropVolume *node;
    ForeachNode(&my->Volumes, node) {
        if (SameLock(node->bv_Lock, wlock) == LOCK_SAME) {
            // Same volume lock.
            Remove(&node->bv_Node);
            wbBackdropFree(&node->bv_Backdrops);
            UnLock(node->bv_Lock);
            FreeVec(node);
            break;
        }
    }

    return TRUE;
}


static IPTR WBBackdrop_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    D(bug("%s: MessageID 0x%lx\n", __func__, (IPTR)msg->MethodID));
    switch (msg->MethodID) {
    METHOD_CASE(WBBackdrop, OM_NEW);
    METHOD_CASE(WBBackdrop, OM_DISPOSE);
    METHOD_CASE(WBBackdrop, WBBM_LockIs);
    METHOD_CASE(WBBackdrop, WBBM_LockNext);
    METHOD_CASE(WBBackdrop, WBBM_LockAdd);
    METHOD_CASE(WBBackdrop, WBBM_LockDel);
    METHOD_CASE(WBBackdrop, WBBM_VolumeAdd);
    METHOD_CASE(WBBackdrop, WBBM_VolumeDel);
    default:               rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

Class *WBBackdrop_MakeClass(struct WorkbookBase *wb)
{
    Class *cl;

    cl = MakeClass( NULL, "rootclass", NULL, sizeof(struct wbBackdrop), 0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBBackdrop_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
        cl->cl_UserData = (IPTR)wb;
    }

    return cl;
}
