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

#include <proto/intuition.h>
#include <proto/icon.h>
#include <proto/utility.h>

#include <clib/alib_protos.h>
#include <workbench/icon.h>

#include "wbdoimage.h"

#ifdef __AROS__
#include "workbook_aros.h"
#else
#include "workbook_vbcc.h"
#endif

#define METHOD_CASE(name, id)   case id: rc = name##__##id(cl, obj, (APTR)msg); break

struct wbDoImage {
    struct Library *di_UtilityBase;
    struct Library *di_IconBase;
    struct Library *di_IntuitionBase;

    struct Screen *Screen;
    struct DiskObject *DiskObject;
    struct DiskObject *ImageObject;

    struct IBox IBox;
};

#define IntuitionBase   my->di_IntuitionBase
#define IconBase        my->di_IconBase
#define UtilityBase     my->di_UtilityBase

static IPTR WBDoImage__OM_NEW(Class *cl, Object *obj, struct opSet *ops)
{
    struct Library *_IntuitionBase;
    struct Library *_IconBase;
    struct Library *_UtilityBase;

    _IntuitionBase = OpenLibrary("intuition.library", 0);
    if (_IntuitionBase) {
        _IconBase = OpenLibrary("icon.library", 0);
        if (_IconBase) {
            _UtilityBase = OpenLibrary("utility.library", 0);
            if (_UtilityBase) {
                IPTR rc = DoSuperMethodA(cl, obj, (Msg)ops);
                if (rc != 0) {
                    obj = (Object *)rc;
                    struct wbDoImage *my = INST_DATA(cl, obj);
                    my->di_UtilityBase = _UtilityBase;
                    my->di_IconBase = _IconBase;
                    my->di_IntuitionBase = _IntuitionBase;
                    my->Screen = NULL;
                    my->DiskObject = NULL;
                    my->ImageObject = NULL;
                    CoerceMethod(cl, obj, OM_SET, ops->ops_AttrList, ops->ops_GInfo);
                    SetAttrs(obj, IA_Recessed, TRUE, IA_EdgesOnly, TRUE, TAG_END);
                    struct Image *im = (struct Image *)obj;
                    D(bug("%s: @%ld,%ld (%ldx%ld)\n", __func__,
                                (IPTR)im->LeftEdge, (IPTR)im->TopEdge,
                                (IPTR)im->Width, (IPTR)im->Height));
                    return rc;
                }
                CloseLibrary(_UtilityBase);
            }
            CloseLibrary(_IconBase);
        }
        CloseLibrary(_IntuitionBase);
    }

    return 0;
}

static IPTR WBDoImage__OM_SET(Class *cl, Object *obj, struct opSet *ops)
{
    struct Image *im = (struct Image *)obj;
    struct wbDoImage *my = INST_DATA(cl, obj);
    struct Screen *screen = my->Screen;
    struct DiskObject *diskobject = my->DiskObject;

    struct TagItem *ti, *tags = ops->ops_AttrList;
    while ((ti = NextTagItem(&tags)) != NULL) {
        switch (ti->ti_Tag) {
        case IA_Screen:
            screen = (struct Screen *)ti->ti_Data;
            break;
        case IA_Data:
            diskobject = (struct DiskObject *)ti->ti_Data;
            break;
        }
    }

    if (screen != my->Screen || diskobject != my->DiskObject) {
        // Lay out image.
        my->Screen = screen;
        my->DiskObject = diskobject;

        if (my->ImageObject) {
            FreeDiskObject(my->ImageObject);
            my->ImageObject = NULL;
        }
        my->ImageObject = DupDiskObject(diskobject, ICONDUPA_DuplicateImages, TRUE, ICONDUPA_DuplicateImageData, TRUE, TAG_END);
        if (my->Screen && my->ImageObject) {
            LayoutIcon(my->ImageObject, my->Screen, TAG_END);
            struct Rectangle rect;
            GetIconRectangle(&my->Screen->RastPort, my->ImageObject, NULL, &rect,
                    ICONDRAWA_Frameless, TRUE,
                    ICONDRAWA_Borderless, TRUE,
                    ICONDRAWA_EraseBackground, TRUE,
                    TAG_END);

            my->IBox.Width = (rect.MaxX - rect.MinX) + 1;
            my->IBox.Height = (rect.MaxY - rect.MinY) + 1;
        } else {
            // For the default IM_ERASE supermethod.
            my->IBox.Width = 32;
            my->IBox.Height = 32;
        }
        my->IBox.Top = (im->Height - my->IBox.Height)/2;
        my->IBox.Left = (im->Width - my->IBox.Width)/2;
    }

    return DoSuperMethodA(cl, obj, (Msg)ops);
}

static IPTR WBDoImage__OM_DISPOSE(Class *cl, Object *obj, Msg msg)
{
    struct wbDoImage *my = INST_DATA(cl, obj);

    if (my->ImageObject) {
        FreeDiskObject(my->ImageObject);
    }

    CloseLibrary(my->di_UtilityBase);
    CloseLibrary(my->di_IconBase);
    CloseLibrary(my->di_IntuitionBase);

    return DoSuperMethodA(cl, obj, msg);
}

static IPTR WBDoImage__IM_DRAW(Class *cl, Object *obj, struct impDraw *imp)
{
    struct wbDoImage *my = INST_DATA(cl, obj);

    if (my->ImageObject) {
        DrawIconState(imp->imp_RPort, my->ImageObject, NULL, my->IBox.Left + imp->imp_Offset.X, my->IBox.Top + imp->imp_Offset.Y, imp->imp_State,
                    ICONDRAWA_Frameless, TRUE,
                    ICONDRAWA_Borderless, TRUE,
                    ICONDRAWA_EraseBackground, TRUE,
                    ICONDRAWA_DrawInfo, imp->imp_DrInfo,
                    TAG_END);
    }

    return DoSuperMethodA(cl, obj, (Msg)imp);
}

static IPTR WBDoImage_dispatcher(Class *cl, Object *obj, Msg msg)
{
    IPTR rc = 0;

    switch (msg->MethodID) {
    METHOD_CASE(WBDoImage, OM_NEW);
    METHOD_CASE(WBDoImage, OM_DISPOSE);
    METHOD_CASE(WBDoImage, OM_SET);
    METHOD_CASE(WBDoImage, IM_DRAW);
    default:               rc = DoSuperMethodA(cl, obj, msg); break;
    }

    return rc;
}

#undef IntuitionBase
#undef IconBase
#undef UtilityBase

Class *WBDoImage_MakeClass(struct Library *IntuitionBase)
{
    Class *cl;

    cl = MakeClass( NULL, "frameiclass", NULL,
                    sizeof(struct wbDoImage),
                    0);
    if (cl != NULL) {
        cl->cl_Dispatcher.h_Entry = HookEntry;
        cl->cl_Dispatcher.h_SubEntry = WBDoImage_dispatcher;
        cl->cl_Dispatcher.h_Data = NULL;
    }

    return cl;
}
