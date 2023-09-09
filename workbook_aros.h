// Copyright 2023, Jason S. McMullan
//

#pragma once

#include <limits.h>
#include <aros/debug.h>
#include <intuition/cghooks.h>
#include <clib/alib_protos.h>
#include <clib/arossupport_protos.h>

static inline LONG STRLEN(CONST_STRPTR s)
{
    CONST_STRPTR _s = s;
    while (*_s++ != '\0');
    return _s - s - 1;
}


// This idiom is used so that we get type-checking on the inputs.
static inline void __inline_RemoveMinNode(struct ExecBase *SysBase, struct MinNode *minnode)
{
	Remove((struct Node *)minnode);
}
#define RemoveMinNode(minnode) __inline_RemoveMinNode(SysBase, minnode)

static inline void __inline_AddTailMinList(struct ExecBase *SysBase, struct MinList *minlist, struct MinNode *minnode)
{
	AddTail((struct List *)(minlist),(struct Node *)(minnode));
}
#define AddTailMinList(minlist, minnode) __inline_AddTailMinList(SysBase, minlist, minnode)

static inline ULONG __inline_UpdateAttrs(Object *o, struct GadgetInfo *gi, ULONG flags, Tag attr1, ...)
    {
        return DoMethod(o, OM_UPDATE, &attr1, gi, flags);
    }
#define UpdateAttrs(o, gi, flags, ...) __inline_UpdateAttrs(o, gi, flags, __VA_ARGS__)


